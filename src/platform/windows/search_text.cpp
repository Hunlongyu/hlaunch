#include "platform/windows/search_text.h"

#include <Windows.h>

#include <limits>

namespace hlaunch::platform::windows {
namespace {

std::optional<std::wstring> utf8ToWide(const std::string_view value)
{
    if (value.empty()) {
        return std::wstring{};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            wide.data(),
            required) != required) {
        return std::nullopt;
    }
    return wide;
}

} // namespace

std::optional<std::wstring> normalizeSearchText(const std::wstring_view value)
{
    if (value.empty()) {
        return std::wstring{};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const int required = LCMapStringEx(
        LOCALE_NAME_INVARIANT,
        LCMAP_LOWERCASE,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr,
        0);
    if (required <= 0) {
        return std::nullopt;
    }
    std::wstring normalized(static_cast<std::size_t>(required), L'\0');
    if (LCMapStringEx(
            LOCALE_NAME_INVARIANT,
            LCMAP_LOWERCASE,
            value.data(),
            static_cast<int>(value.size()),
            normalized.data(),
            required,
            nullptr,
            nullptr,
            0) != required) {
        return std::nullopt;
    }
    return normalized;
}

std::optional<std::wstring> normalizeSearchText(const std::string_view utf8Value)
{
    const auto wide = utf8ToWide(utf8Value);
    return wide ? normalizeSearchText(*wide) : std::nullopt;
}

} // namespace hlaunch::platform::windows
