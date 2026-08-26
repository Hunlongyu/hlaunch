#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace hlaunch::platform::windows {

[[nodiscard]] std::optional<std::wstring> normalizeSearchText(
    std::wstring_view value);
[[nodiscard]] std::optional<std::wstring> normalizeSearchText(
    std::string_view utf8Value);

} // namespace hlaunch::platform::windows
