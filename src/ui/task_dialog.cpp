#include "ui/task_dialog.h"

#include <CommCtrl.h>

#include <string>

namespace hlaunch::ui {
namespace {

using TaskDialogIndirectFunction = HRESULT(WINAPI*)(
    const TASKDIALOGCONFIG*, int*, int*, BOOL*);

TaskDialogIndirectFunction taskDialogFunction() noexcept
{
    static const HMODULE commonControls = LoadLibraryW(L"comctl32.dll");
    if (!commonControls) {
        return nullptr;
    }
    return reinterpret_cast<TaskDialogIndirectFunction>(
        GetProcAddress(commonControls, "TaskDialogIndirect")); // NOLINT(performance-no-int-to-ptr): GetProcAddress returns the requested function pointer.
}

PCWSTR taskIcon(const TaskDialogIcon icon) noexcept
{
    switch (icon) {
    case TaskDialogIcon::Warning:
        return TD_WARNING_ICON;
    case TaskDialogIcon::Error:
        return TD_ERROR_ICON;
    case TaskDialogIcon::Information:
    default:
        return TD_INFORMATION_ICON;
    }
}

UINT fallbackIcon(const TaskDialogIcon icon) noexcept
{
    switch (icon) {
    case TaskDialogIcon::Warning:
        return MB_ICONWARNING;
    case TaskDialogIcon::Error:
        return MB_ICONERROR;
    case TaskDialogIcon::Information:
    default:
        return MB_ICONINFORMATION;
    }
}

std::wstring fallbackText(
    const std::wstring_view instruction,
    const std::wstring_view details)
{
    std::wstring text{instruction};
    if (!details.empty()) {
        text.append(L"\n\n");
        text.append(details);
    }
    return text;
}

TASKDIALOGCONFIG configuration(
    const HWND owner,
    const std::wstring& title,
    const std::wstring& instruction,
    const std::wstring& details,
    const TaskDialogIcon icon) noexcept
{
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = owner;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION
        | TDF_POSITION_RELATIVE_TO_WINDOW
        | TDF_SIZE_TO_CONTENT;
    config.pszWindowTitle = title.c_str();
    config.pszMainInstruction = instruction.c_str();
    config.pszContent = details.empty() ? nullptr : details.c_str();
    config.pszMainIcon = taskIcon(icon);
    return config;
}

} // namespace

void showTaskMessage(
    const HWND owner,
    const std::wstring_view title,
    const std::wstring_view instruction,
    const TaskDialogIcon icon,
    const std::wstring_view details)
{
    const std::wstring titleText{title};
    const std::wstring instructionText{instruction};
    const std::wstring detailsText{details};
    auto config = configuration(owner, titleText, instructionText, detailsText, icon);
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    const auto showDialog = taskDialogFunction();
    if (!showDialog || FAILED(showDialog(&config, nullptr, nullptr, nullptr))) {
        const auto text = fallbackText(instruction, details);
        MessageBoxW(owner, text.c_str(), titleText.c_str(), MB_OK | fallbackIcon(icon));
    }
}

bool confirmTask(
    const HWND owner,
    const std::wstring_view title,
    const std::wstring_view instruction,
    const std::wstring_view details,
    const TaskDialogIcon icon)
{
    const std::wstring titleText{title};
    const std::wstring instructionText{instruction};
    const std::wstring detailsText{details};
    auto config = configuration(owner, titleText, instructionText, detailsText, icon);
    config.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
    config.nDefaultButton = IDNO;
    int selected{};
    const auto showDialog = taskDialogFunction();
    if (showDialog
        && SUCCEEDED(showDialog(&config, &selected, nullptr, nullptr))) {
        return selected == IDYES;
    }
    const auto text = fallbackText(instruction, details);
    return MessageBoxW(
               owner,
               text.c_str(),
               titleText.c_str(),
               MB_YESNO | MB_DEFBUTTON2 | fallbackIcon(icon))
        == IDYES;
}

} // namespace hlaunch::ui
