#pragma once

#include <Windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace hlaunch::ui {

[[nodiscard]] std::optional<std::wstring> showTextPromptDialog(
    HWND owner,
    std::wstring_view title,
    std::wstring_view label,
    std::wstring_view initialValue);

} // namespace hlaunch::ui
