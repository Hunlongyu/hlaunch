#pragma once

#include <Windows.h>

#include <string_view>

namespace hlaunch::ui {

enum class TaskDialogIcon {
    Information,
    Warning,
    Error,
};

void showTaskMessage(
    HWND owner,
    std::wstring_view title,
    std::wstring_view instruction,
    TaskDialogIcon icon = TaskDialogIcon::Information,
    std::wstring_view details = {});

[[nodiscard]] bool confirmTask(
    HWND owner,
    std::wstring_view title,
    std::wstring_view instruction,
    std::wstring_view details = {},
    TaskDialogIcon icon = TaskDialogIcon::Warning);

} // namespace hlaunch::ui
