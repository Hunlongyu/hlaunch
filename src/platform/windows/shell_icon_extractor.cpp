#include "platform/windows/shell_icon_extractor.h"

#include "platform/windows/svg_icon_renderer.h"

#include <Windows.h>
#include <Shellapi.h>
#include <Shobjidl.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

namespace hlaunch::platform::windows {
namespace {

std::optional<std::wstring> utf8ToWide(const std::string_view value)
{
    if (value.empty())
    {
        return std::wstring{};
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                              static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
    {
        return std::nullopt;
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), required) != required)
    {
        return std::nullopt;
    }
    return result;
}

std::wstring lowerExtension(const std::wstring &path)
{
    auto extension = std::filesystem::path{path}.extension().wstring();
    std::ranges::transform(extension, extension.begin(),
                           [](const wchar_t value) { return std::towlower(value); });
    return extension;
}

std::optional<IconPixels> extractExplicitIconPixels(
    const std::wstring& path,
    const std::uint32_t pixelSize)
{
    if (path.empty()) {
        return std::nullopt;
    }
    const auto extension = lowerExtension(path);
    if (extension == L".svg") {
        return decodeSvgFileToPixels(path, pixelSize);
    }
    if (extension == L".ico" || extension == L".png") {
        if (auto pixels = decodeImageFileToPixels(path, pixelSize)) {
            return pixels;
        }
    }

    HICON extracted{};
    UINT resourceId{};
    const auto count = PrivateExtractIconsW(
        path.c_str(),
        0,
        static_cast<int>(pixelSize),
        static_cast<int>(pixelSize),
        &extracted,
        &resourceId,
        1,
        LR_DEFAULTCOLOR);
    wil::unique_hicon icon{extracted};
    if (count == 0U || count == std::numeric_limits<UINT>::max() || !icon) {
        return std::nullopt;
    }
    return convertIconToPixels(icon.get(), pixelSize);
}

std::optional<IconPixels> extractShellItemPixels(
    const std::wstring& target,
    const std::uint32_t pixelSize)
{
    if (target.empty()) {
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
    winrt::com_ptr<IShellItemImageFactory> factory{};
    if (FAILED(SHCreateItemFromParsingName(
        target.c_str(), nullptr, IID_PPV_ARGS(factory.put())))) {
        return std::nullopt;
    }
    wil::unique_hbitmap bitmap{};
    const SIZE requested{
        static_cast<LONG>(pixelSize),
        static_cast<LONG>(pixelSize),
    };
    if (FAILED(factory->GetImage(
        requested,
        SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK,
        bitmap.put()))) {
        return std::nullopt;
    }
    return convertBitmapToPixels(bitmap.get(), pixelSize);
}

wil::unique_hicon extractTargetIcon(const std::wstring &target)
{
    if (target.empty())
    {
        return {};
    }

    SHFILEINFOW information{};
    UINT flags = SHGFI_ICON | SHGFI_LARGEICON;
    DWORD attributes{};
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        flags |= SHGFI_USEFILEATTRIBUTES;
        attributes = FILE_ATTRIBUTE_NORMAL;
    }
    if (SHGetFileInfoW(target.c_str(), attributes, &information, sizeof(information), flags) == 0)
    {
        return {};
    }
    return wil::unique_hicon{information.hIcon};
}

} // namespace

std::optional<IconPixels>
extractShellIconPixels(const std::string &target, const std::optional<std::string> &iconPath,
                       const std::uint32_t pixelSize)
{
    if (iconPath)
    {
        if (const auto path = utf8ToWide(*iconPath))
        {
            if (auto pixels = extractExplicitIconPixels(*path, pixelSize)) {
                return pixels;
            }
        }
    }
    if (const auto path = utf8ToWide(target))
    {
        if (lowerExtension(*path) == L".exe") {
            if (auto pixels = extractExplicitIconPixels(*path, pixelSize)) {
                return pixels;
            }
        }
        if (auto pixels = extractShellItemPixels(*path, pixelSize)) {
            return pixels;
        }
        if (auto icon = extractTargetIcon(*path)) {
            return convertIconToPixels(icon.get(), pixelSize);
        }
    }
    return std::nullopt;
}

} // namespace hlaunch::platform::windows
