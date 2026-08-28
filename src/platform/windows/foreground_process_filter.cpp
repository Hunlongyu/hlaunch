#include "platform/windows/foreground_process_filter.h"

#include <wil/resource.h>

#include <algorithm>
#include <limits>

namespace hlaunch::platform::windows {
namespace {

std::optional<std::wstring> widenUtf8(const std::string_view value)
{
    if (value.empty() || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const auto sourceLength = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), sourceLength, nullptr, 0);
    if (required <= 0) {
        return std::nullopt;
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), sourceLength,
            result.data(), required) != required) {
        return std::nullopt;
    }
    return result;
}

bool equalsIgnoreCase(const std::wstring_view left, const std::wstring_view right) noexcept
{
    if (left.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())
        || right.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    return CompareStringOrdinal(
               left.data(), static_cast<int>(left.size()),
               right.data(), static_cast<int>(right.size()), TRUE)
        == CSTR_EQUAL;
}

bool contains(
    const std::vector<std::wstring>& names,
    const std::wstring_view executableName) noexcept
{
    return std::ranges::any_of(names, [executableName](const std::wstring& configured) {
        return equalsIgnoreCase(configured, executableName);
    });
}

std::vector<std::wstring> widenNames(const std::vector<std::string>& names)
{
    std::vector<std::wstring> result{};
    result.reserve(names.size());
    for (const auto& name : names) {
        if (auto widened = widenUtf8(name)) {
            result.push_back(std::move(*widened));
        }
    }
    return result;
}

} // namespace

ForegroundProcessFilter::ForegroundProcessFilter(const core::ScreenEdgeConfig& config)
    : blocklist_{widenNames(config.foregroundProcessBlocklist)}
    , allowlist_{widenNames(config.foregroundProcessAllowlist)}
{
}

bool ForegroundProcessFilter::active() const noexcept
{
    return !blocklist_.empty() || !allowlist_.empty();
}

bool ForegroundProcessFilter::suppresses(
    const std::optional<std::wstring_view> executableName) const noexcept
{
    if (!active()) {
        return false;
    }
    if (!executableName) {
        return !allowlist_.empty();
    }
    if (contains(allowlist_, *executableName)) {
        return false;
    }
    if (contains(blocklist_, *executableName)) {
        return true;
    }
    return !allowlist_.empty();
}

std::optional<std::wstring> executableNameForWindow(const HWND window) noexcept
{
    DWORD processId{};
    if (!window || !GetWindowThreadProcessId(window, &processId) || processId == 0) {
        return std::nullopt;
    }
    wil::unique_process_handle process{
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)};
    if (!process) {
        return std::nullopt;
    }

    std::wstring path(32'768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length) || length == 0) {
        return std::nullopt;
    }
    path.resize(length);
    const auto separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        path.erase(0, separator + 1);
    }
    return path.empty() ? std::nullopt : std::optional<std::wstring>{std::move(path)};
}

} // namespace hlaunch::platform::windows
