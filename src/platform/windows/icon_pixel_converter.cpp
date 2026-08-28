#include "platform/windows/icon_pixel_converter.h"

#include <wincodec.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hlaunch::platform::windows {
namespace {

class ComApartment final
{
public:
    ComApartment() noexcept
        : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
    {
    }

    ~ComApartment()
    {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool available() const noexcept
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_{};
};

std::optional<IconPixels> convertBitmapSourceToPixels(
    IWICImagingFactory* const factory,
    IWICBitmapSource* const source,
    const std::uint32_t pixelSize)
{
    if (!factory || !source) {
        return std::nullopt;
    }

    UINT sourceWidth{};
    UINT sourceHeight{};
    if (FAILED(source->GetSize(&sourceWidth, &sourceHeight))
        || sourceWidth == 0U || sourceHeight == 0U) {
        return std::nullopt;
    }

    const auto size = std::clamp<std::uint32_t>(pixelSize, 16U, 256U);
    const auto scale = std::min(
        static_cast<double>(size) / static_cast<double>(sourceWidth),
        static_cast<double>(size) / static_cast<double>(sourceHeight));
    const auto scaledWidth = std::max<UINT>(
        1U, static_cast<UINT>(std::lround(static_cast<double>(sourceWidth) * scale)));
    const auto scaledHeight = std::max<UINT>(
        1U, static_cast<UINT>(std::lround(static_cast<double>(sourceHeight) * scale)));

    // Convert to premultiplied alpha before scaling. Interpolating straight-alpha
    // RGB first lets colors from transparent pixels bleed into visible edges.
    winrt::com_ptr<IWICFormatConverter> premultipliedSource{};
    if (FAILED(factory->CreateFormatConverter(premultipliedSource.put()))
        || FAILED(premultipliedSource->Initialize(
            source,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom))) {
        return std::nullopt;
    }

    winrt::com_ptr<IWICBitmapSource> scaledSource{};
    if (scaledWidth != sourceWidth || scaledHeight != sourceHeight) {
        winrt::com_ptr<IWICBitmapScaler> scaler{};
        if (FAILED(factory->CreateBitmapScaler(scaler.put()))
            || FAILED(scaler->Initialize(
                premultipliedSource.get(),
                scaledWidth,
                scaledHeight,
                WICBitmapInterpolationModeFant))) {
            return std::nullopt;
        }
        scaledSource = scaler.as<IWICBitmapSource>();
    }
    else {
        scaledSource = premultipliedSource.as<IWICBitmapSource>();
    }

    const auto scaledStride = scaledWidth * 4U;
    std::vector<std::uint8_t> scaledValues(
        static_cast<std::size_t>(scaledStride) * scaledHeight);
    if (FAILED(scaledSource->CopyPixels(
        nullptr,
        scaledStride,
        static_cast<UINT>(scaledValues.size()),
        scaledValues.data()))) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < scaledValues.size(); index += 4U) {
        const auto alpha = scaledValues[index + 3U];
        scaledValues[index] = std::min(scaledValues[index], alpha);
        scaledValues[index + 1U] = std::min(scaledValues[index + 1U], alpha);
        scaledValues[index + 2U] = std::min(scaledValues[index + 2U], alpha);
    }

    IconPixels result{
        .width = size,
        .height = size,
        .values = std::vector<std::uint8_t>(
            static_cast<std::size_t>(size) * size * 4U),
    };
    const auto xOffset = (size - scaledWidth) / 2U;
    const auto yOffset = (size - scaledHeight) / 2U;
    for (UINT row = 0; row < scaledHeight; ++row) {
        const auto* sourceRow = scaledValues.data()
            + static_cast<std::size_t>(row) * scaledStride;
        auto* destinationRow = result.values.data()
            + (static_cast<std::size_t>(row + yOffset) * size + xOffset) * 4U;
        std::copy_n(sourceRow, scaledStride, destinationRow);
    }
    return result;
}

winrt::com_ptr<IWICImagingFactory> createImagingFactory()
{
    winrt::com_ptr<IWICImagingFactory> factory{};
    if (FAILED(CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory),
        factory.put_void()))) {
        return {};
    }
    return factory;
}

std::optional<UINT> selectBestFrame(
    IWICBitmapDecoder* const decoder,
    const std::uint32_t pixelSize)
{
    UINT frameCount{};
    if (!decoder || FAILED(decoder->GetFrameCount(&frameCount)) || frameCount == 0U) {
        return std::nullopt;
    }

    std::optional<UINT> exact{};
    std::optional<UINT> larger{};
    std::optional<UINT> smaller{};
    std::uint64_t largerArea = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t smallerArea{};
    for (UINT index = 0; index < frameCount; ++index) {
        winrt::com_ptr<IWICBitmapFrameDecode> frame{};
        UINT width{};
        UINT height{};
        if (FAILED(decoder->GetFrame(index, frame.put()))
            || FAILED(frame->GetSize(&width, &height))
            || width == 0U || height == 0U) {
            continue;
        }
        if (width == pixelSize && height == pixelSize) {
            exact = index;
            break;
        }
        const auto area = static_cast<std::uint64_t>(width) * height;
        if (width >= pixelSize && height >= pixelSize) {
            if (area < largerArea) {
                largerArea = area;
                larger = index;
            }
        }
        else if (area > smallerArea) {
            smallerArea = area;
            smaller = index;
        }
    }
    return exact ? exact : larger ? larger : smaller;
}

} // namespace

std::optional<IconPixels> convertIconToPixels(const HICON icon,
                                              const std::uint32_t pixelSize)
{
    if (!icon)
    {
        return std::nullopt;
    }

    const ComApartment apartment{};
    if (!apartment.available()) {
        return std::nullopt;
    }
    auto factory = createImagingFactory();
    if (!factory) {
        return std::nullopt;
    }
    winrt::com_ptr<IWICBitmap> source{};
    if (FAILED(factory->CreateBitmapFromHICON(icon, source.put()))) {
        return std::nullopt;
    }
    return convertBitmapSourceToPixels(factory.get(), source.get(), pixelSize);
}

std::optional<IconPixels> convertBitmapToPixels(
    const HBITMAP bitmap,
    const std::uint32_t pixelSize)
{
    if (!bitmap) {
        return std::nullopt;
    }
    const ComApartment apartment{};
    if (!apartment.available()) {
        return std::nullopt;
    }
    auto factory = createImagingFactory();
    if (!factory) {
        return std::nullopt;
    }
    winrt::com_ptr<IWICBitmap> source{};
    if (FAILED(factory->CreateBitmapFromHBITMAP(
        bitmap, nullptr, WICBitmapUseAlpha, source.put()))) {
        return std::nullopt;
    }
    return convertBitmapSourceToPixels(factory.get(), source.get(), pixelSize);
}

std::optional<IconPixels> decodeImageFileToPixels(
    const std::wstring& path,
    const std::uint32_t pixelSize)
{
    if (path.empty()) {
        return std::nullopt;
    }
    const ComApartment apartment{};
    if (!apartment.available()) {
        return std::nullopt;
    }
    auto factory = createImagingFactory();
    if (!factory) {
        return std::nullopt;
    }
    winrt::com_ptr<IWICBitmapDecoder> decoder{};
    if (FAILED(factory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        decoder.put()))) {
        return std::nullopt;
    }
    const auto frameIndex = selectBestFrame(decoder.get(), pixelSize);
    if (!frameIndex) {
        return std::nullopt;
    }
    winrt::com_ptr<IWICBitmapFrameDecode> frame{};
    if (FAILED(decoder->GetFrame(*frameIndex, frame.put()))) {
        return std::nullopt;
    }
    return convertBitmapSourceToPixels(factory.get(), frame.get(), pixelSize);
}

} // namespace hlaunch::platform::windows
