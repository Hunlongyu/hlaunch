#pragma once

#include "core/data_model.h"

#include <Windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace hlaunch::ui {

[[nodiscard]] std::optional<std::wstring> showTextPromptDialog(
    HWND owner,
    std::wstring_view title,
    std::wstring_view label,
    std::wstring_view initialValue,
    core::ThemeMode themeMode = core::ThemeMode::Dark);

} // namespace hlaunch::ui
