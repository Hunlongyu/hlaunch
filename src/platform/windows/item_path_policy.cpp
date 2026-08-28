#include "platform/windows/item_path_policy.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hlaunch::platform::windows {
namespace {

std::expected<std::wstring, ItemPathError> utf8ToWide(const std::string_view value)
{
    if (value.empty())
    {
        return std::wstring{};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::InvalidUtf8, GetLastError()});
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), required) != required)
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::InvalidUtf8, GetLastError()});
    }
    return result;
}

std::expected<std::string, ItemPathError> wideToUtf8(const std::wstring_view value)
{
    if (value.empty())
    {
        return std::string{};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0)
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::InvalidUtf8, GetLastError()});
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), required, nullptr, nullptr) != required)
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::InvalidUtf8, GetLastError()});
    }
    return result;
}

std::expected<std::wstring, ItemPathError> expandEnvironment(const std::wstring_view value)
{
    if (value.empty())
    {
        return std::wstring{};
    }
    const std::wstring input{value};
    const DWORD required = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (required == 0)
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::ExpansionFailed, GetLastError()});
    }
    std::wstring result(required, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(input.c_str(), result.data(), required);
    if (written == 0 || written > required)
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::ExpansionFailed, GetLastError()});
    }
    result.resize(written - 1U);
    return result;
}

std::expected<std::string, ItemPathError> resolvePath(
    const std::string_view value,
    const std::filesystem::path &executableDirectory)
{
    auto wide = utf8ToWide(value);
    if (!wide)
    {
        return std::unexpected(wide.error());
    }
    auto expanded = expandEnvironment(*wide);
    if (!expanded)
    {
        return std::unexpected(expanded.error());
    }
    std::filesystem::path path{*expanded};
    if ((path.has_root_name() && !path.has_root_directory())
        || (!path.has_root_name() && path.has_root_directory()))
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::InvalidPath});
    }
    if (path.is_relative())
    {
        path = executableDirectory / path;
    }
    return wideToUtf8(path.lexically_normal().wstring());
}

bool equalComponent(const std::filesystem::path &left, const std::filesystem::path &right)
{
    const auto leftText = left.native();
    const auto rightText = right.native();
    return CompareStringOrdinal(
        leftText.data(), static_cast<int>(leftText.size()),
        rightText.data(), static_cast<int>(rightText.size()), TRUE) == CSTR_EQUAL;
}

std::optional<std::filesystem::path> relativeDescendant(
    const std::filesystem::path &path,
    const std::filesystem::path &base)
{
    const auto normalizedPath = path.lexically_normal();
    const auto normalizedBase = base.lexically_normal();
    auto pathPart = normalizedPath.begin();
    auto basePart = normalizedBase.begin();
    for (; basePart != normalizedBase.end() && pathPart != normalizedPath.end();
         ++basePart, ++pathPart)
    {
        if (!equalComponent(*pathPart, *basePart))
        {
            return std::nullopt;
        }
    }
    if (basePart != normalizedBase.end())
    {
        return std::nullopt;
    }

    std::filesystem::path relative{};
    for (; pathPart != normalizedPath.end(); ++pathPart)
    {
        relative /= *pathPart;
    }
    return relative.empty() ? std::filesystem::path{L"."} : relative;
}

std::expected<std::string, ItemPathError> makePortablePath(
    const std::string_view value,
    const ItemPathContext &context)
{
    if (!context.portable || value.find('%') != std::string_view::npos)
    {
        return std::string{value};
    }
    auto wide = utf8ToWide(value);
    if (!wide)
    {
        return std::unexpected(wide.error());
    }
    const std::filesystem::path path{*wide};
    if ((path.has_root_name() && !path.has_root_directory())
        || (!path.has_root_name() && path.has_root_directory()))
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::InvalidPath});
    }
    if (path.is_relative())
    {
        return std::string{value};
    }
    const auto relative = relativeDescendant(path, context.executableDirectory);
    if (!relative)
    {
        return std::string{value};
    }
    return wideToUtf8(relative->wstring());
}

template <typename Transform>
std::expected<core::LaunchItem, ItemPathError> transformItemPaths(
    const core::LaunchItem &item,
    Transform transform)
{
    core::LaunchItem transformed = item;
    if (item.type != core::ItemType::Url)
    {
        auto target = transform(item.target);
        if (!target)
        {
            return std::unexpected(target.error());
        }
        transformed.target = std::move(*target);
    }
    if (item.workingDirectory && !item.workingDirectory->empty())
    {
        auto workingDirectory = transform(*item.workingDirectory);
        if (!workingDirectory)
        {
            return std::unexpected(workingDirectory.error());
        }
        transformed.workingDirectory = std::move(*workingDirectory);
    }
    if (item.icon && !item.icon->empty())
    {
        auto icon = transform(*item.icon);
        if (!icon)
        {
            return std::unexpected(icon.error());
        }
        transformed.icon = std::move(*icon);
    }
    return transformed;
}

} // namespace

std::expected<core::LaunchItem, ItemPathError> resolveItemPaths(
    const core::LaunchItem &item,
    const ItemPathContext &context)
{
    if (!context.executableDirectory.is_absolute())
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::InvalidContext});
    }
    return transformItemPaths(item, [&](const std::string_view value) {
        return resolvePath(value, context.executableDirectory);
    });
}

std::expected<core::LaunchItem, ItemPathError> makeItemPathsPortable(
    const core::LaunchItem &item,
    const ItemPathContext &context)
{
    if (!context.executableDirectory.is_absolute())
    {
        return std::unexpected(ItemPathError{ItemPathErrorCode::InvalidContext});
    }
    return transformItemPaths(item, [&](const std::string_view value) {
        return makePortablePath(value, context);
    });
}

} // namespace hlaunch::platform::windows
