#include "platform/windows/icon_pixel_converter.h"

#include <wincodec.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <algorithm>

namespace hlaunch::platform::windows {

std::optional<IconPixels> convertIconToPixels(const HICON icon,
                                              const std::uint32_t pixelSize)
{
    if (!icon)
    {
        return std::nullopt;
    }

    const auto apartmentResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool apartmentOwned = SUCCEEDED(apartmentResult);
    const auto apartmentCleanup = wil::scope_exit([apartmentOwned] {
        if (apartmentOwned)
        {
            CoUninitialize();
        }
    });
    if (FAILED(apartmentResult) && apartmentResult != RPC_E_CHANGED_MODE)
    {
        return std::nullopt;
    }

    winrt::com_ptr<IWICImagingFactory> factory{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IWICImagingFactory), factory.put_void())))
    {
        return std::nullopt;
    }
    winrt::com_ptr<IWICBitmap> source{};
    if (FAILED(factory->CreateBitmapFromHICON(icon, source.put())))
    {
        return std::nullopt;
    }

    const auto size = std::clamp<std::uint32_t>(pixelSize, 16U, 256U);
    winrt::com_ptr<IWICBitmapScaler> scaler{};
    if (FAILED(factory->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(source.get(), size, size, WICBitmapInterpolationModeFant)))
    {
        return std::nullopt;
    }
    winrt::com_ptr<IWICFormatConverter> converter{};
    if (FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)))
    {
        return std::nullopt;
    }

    IconPixels result{
        .width = size,
        .height = size,
        .values = std::vector<std::uint8_t>(static_cast<std::size_t>(size) * size * 4U),
    };
    const auto stride = size * 4U;
    if (FAILED(converter->CopyPixels(nullptr, stride,
                                     static_cast<UINT>(result.values.size()),
                                     result.values.data())))
    {
        return std::nullopt;
    }
    return result;
}

} // namespace hlaunch::platform::windows
