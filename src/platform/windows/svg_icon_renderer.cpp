#include "platform/windows/svg_icon_renderer.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>

namespace hlaunch::platform::windows {
namespace {

constexpr std::uintmax_t maximumSvgBytes = 8U * 1024U * 1024U;

winrt::com_ptr<ID3D11Device> createD3dDevice()
{
    constexpr std::array driverTypes{
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
    };
    for (const auto driverType : driverTypes) {
        winrt::com_ptr<ID3D11Device> device{};
        winrt::com_ptr<ID3D11DeviceContext> immediateContext{};
        if (SUCCEEDED(D3D11CreateDevice(
                nullptr,
                driverType,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                device.put(),
                nullptr,
                immediateContext.put()))) {
            return device;
        }
    }
    return {};
}

winrt::com_ptr<ID2D1DeviceContext5> createSvgDeviceContext()
{
    const auto d3dDevice = createD3dDevice();
    if (!d3dDevice) {
        return {};
    }
    const auto dxgiDevice = d3dDevice.try_as<IDXGIDevice>();
    if (!dxgiDevice) {
        return {};
    }

    D2D1_FACTORY_OPTIONS factoryOptions{};
    winrt::com_ptr<ID2D1Factory1> factory{};
    if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1),
            &factoryOptions,
            factory.put_void()))) {
        return {};
    }
    winrt::com_ptr<ID2D1Device> d2dDevice{};
    if (FAILED(factory->CreateDevice(dxgiDevice.get(), d2dDevice.put()))) {
        return {};
    }
    winrt::com_ptr<ID2D1DeviceContext> baseContext{};
    if (FAILED(d2dDevice->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, baseContext.put()))) {
        return {};
    }
    return baseContext.try_as<ID2D1DeviceContext5>();
}

bool safeSvgFileSize(const std::wstring& path) noexcept
{
    std::error_code error{};
    const auto size = std::filesystem::file_size(path, error);
    return !error && size > 0U && size <= maximumSvgBytes;
}

} // namespace

std::optional<IconPixels> decodeSvgFileToPixels(
    const std::wstring& path,
    const std::uint32_t pixelSize)
{
    if (path.empty() || !safeSvgFileSize(path)) {
        return std::nullopt;
    }
    const auto apartmentResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto apartmentCleanup = wil::scope_exit([apartmentResult] {
        if (SUCCEEDED(apartmentResult)) {
            CoUninitialize();
        }
    });
    if (FAILED(apartmentResult) && apartmentResult != RPC_E_CHANGED_MODE) {
        return std::nullopt;
    }
    const auto size = std::clamp<std::uint32_t>(pixelSize, 16U, 256U);
    const auto context = createSvgDeviceContext();
    if (!context) {
        return std::nullopt;
    }

    winrt::com_ptr<IStream> stream{};
    if (FAILED(SHCreateStreamOnFileEx(
            path.c_str(),
            STGM_READ | STGM_SHARE_DENY_WRITE,
            FILE_ATTRIBUTE_NORMAL,
            FALSE,
            nullptr,
            stream.put()))) {
        return std::nullopt;
    }

    winrt::com_ptr<ID2D1SvgDocument> document{};
    if (FAILED(context->CreateSvgDocument(
            stream.get(),
            D2D1_SIZE_F{static_cast<float>(size), static_cast<float>(size)},
            document.put()))) {
        return std::nullopt;
    }
    winrt::com_ptr<ID2D1SvgElement> root{};
    document->GetRoot(root.put());
    if (!root) {
        return std::nullopt;
    }
    constexpr D2D1_SVG_LENGTH fullViewport{
        100.0F,
        D2D1_SVG_LENGTH_UNITS_PERCENTAGE,
    };
    if (FAILED(root->SetAttributeValue(L"width", fullViewport))
        || FAILED(root->SetAttributeValue(L"height", fullViewport))) {
        return std::nullopt;
    }

    const D2D1_BITMAP_PROPERTIES1 targetProperties{
        D2D1_PIXEL_FORMAT{
            DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED,
        },
        96.0F,
        96.0F,
        D2D1_BITMAP_OPTIONS_TARGET,
        nullptr,
    };
    winrt::com_ptr<ID2D1Bitmap1> target{};
    if (FAILED(context->CreateBitmap(
            D2D1_SIZE_U{size, size},
            nullptr,
            0,
            &targetProperties,
            target.put()))) {
        return std::nullopt;
    }

    context->SetTarget(target.get());
    context->BeginDraw();
    context->SetTransform(D2D1_MATRIX_3X2_F{1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F});
    context->Clear(D2D1_COLOR_F{0.0F, 0.0F, 0.0F, 0.0F});
    context->DrawSvgDocument(document.get());
    const auto drawResult = context->EndDraw();
    context->SetTarget(nullptr);
    if (FAILED(drawResult)) {
        return std::nullopt;
    }

    const D2D1_BITMAP_PROPERTIES1 readbackProperties{
        targetProperties.pixelFormat,
        targetProperties.dpiX,
        targetProperties.dpiY,
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        nullptr,
    };
    winrt::com_ptr<ID2D1Bitmap1> readback{};
    if (FAILED(context->CreateBitmap(
            D2D1_SIZE_U{size, size},
            nullptr,
            0,
            &readbackProperties,
            readback.put()))
        || FAILED(readback->CopyFromBitmap(nullptr, target.get(), nullptr))) {
        return std::nullopt;
    }

    D2D1_MAPPED_RECT mapped{};
    if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped)) || !mapped.bits) {
        return std::nullopt;
    }
    const auto unmap = wil::scope_exit([&readback] { readback->Unmap(); });
    const auto rowBytes = static_cast<std::size_t>(size) * 4U;
    if (mapped.pitch < rowBytes) {
        return std::nullopt;
    }
    IconPixels result{
        .width = size,
        .height = size,
        .values = std::vector<std::uint8_t>(rowBytes * size),
    };
    for (std::uint32_t row = 0; row < size; ++row) {
        std::copy_n(
            mapped.bits + static_cast<std::size_t>(row) * mapped.pitch,
            rowBytes,
            result.values.data() + static_cast<std::size_t>(row) * rowBytes);
    }
    for (std::size_t index = 0; index < result.values.size(); index += 4U) {
        const auto alpha = result.values[index + 3U];
        result.values[index] = std::min(result.values[index], alpha);
        result.values[index + 1U] = std::min(result.values[index + 1U], alpha);
        result.values[index + 2U] = std::min(result.values[index + 2U], alpha);
    }
    return result;
}

} // namespace hlaunch::platform::windows
