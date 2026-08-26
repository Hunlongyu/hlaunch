#include "platform/windows/shell_icon_extractor.h"

#include <Windows.h>
#include <Shellapi.h>
#include <wil/resource.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
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

wil::unique_hicon extractExplicitIcon(const std::wstring &path,
                                      const std::uint32_t pixelSize)
{
    if (path.empty())
    {
        return {};
    }
    if (lowerExtension(path) == L".ico")
    {
        return wil::unique_hicon{static_cast<HICON>(LoadImageW(
            nullptr, path.c_str(), IMAGE_ICON, static_cast<int>(pixelSize),
            static_cast<int>(pixelSize), LR_LOADFROMFILE))};
    }

    HICON large{};
    HICON small{};
    if (ExtractIconExW(path.c_str(), 0, &large, &small, 1) == 0)
    {
        return {};
    }
    if (small)
    {
        DestroyIcon(small);
    }
    return wil::unique_hicon{large};
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
    wil::unique_hicon icon{};
    if (iconPath)
    {
        if (const auto path = utf8ToWide(*iconPath))
        {
            icon = extractExplicitIcon(*path, pixelSize);
        }
    }
    if (!icon)
    {
        if (const auto path = utf8ToWide(target))
        {
            icon = extractTargetIcon(*path);
        }
    }
    return icon ? convertIconToPixels(icon.get(), pixelSize) : std::nullopt;
}

} // namespace hlaunch::platform::windows
