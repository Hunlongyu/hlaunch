#include "ui/text_prompt_dialog.h"
#include "ui/dpi_layout.h"

#include "ui/native_dialog_template.h"
#include "ui/system_appearance.h"
#include "ui/task_dialog.h"
#include "ui/visual_style.h"

#include <algorithm>

namespace hlaunch::ui {
namespace {

constexpr int idValue = 5101;
constexpr int promptWidth = 390;
constexpr int promptHeight = 160;

struct PromptState {
    std::wstring title;
    std::wstring label;
    std::wstring initialValue;
    SystemUiFont systemUiFont{};
    std::optional<std::wstring> result;
    std::vector<DialogControlLayout> controlLayouts;
};

void refreshSystemAppearance(const HWND dialog, PromptState& state)
{
    static_cast<void>(state.systemUiFont.refresh(GetDpiForWindow(dialog)));
    applySystemUiFont(dialog, state.systemUiFont.get());
    applyNativeWindowStyle(dialog, false);
    EnumChildWindows(
        dialog,
        [](const HWND child, const LPARAM) noexcept -> BOOL {
            applyNativeControlStyle(child, false);
            return TRUE;
        },
        0);
    RedrawWindow(
        dialog, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void centerDialog(const HWND dialog, const HWND owner)
{
    RECT ownerBounds{};
    const UINT dpi = GetDpiForWindow(dialog);
    const int width = scaleDip(promptWidth, dpi);
    const int height = scaleDip(promptHeight, dpi);
    GetWindowRect(owner, &ownerBounds);
    const int x = ownerBounds.left
        + std::max<LONG>(0, (ownerBounds.right - ownerBounds.left - width) / 2);
    const int y = ownerBounds.top
        + std::max<LONG>(0, (ownerBounds.bottom - ownerBounds.top - height) / 2);
    SetWindowPos(dialog, nullptr, x, y, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER);
}

INT_PTR CALLBACK promptProcedure(
    const HWND dialog, const UINT message, const WPARAM wParam, const LPARAM lParam) noexcept
{
    try {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<PromptState*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        SetWindowTextW(dialog, state->title.c_str());
        centerDialog(dialog, GetParent(dialog));
        static_cast<void>(state->systemUiFont.refresh(GetDpiForWindow(dialog)));
        const auto font = state->systemUiFont.get();
        const UINT dpi = GetDpiForWindow(dialog);
        auto add = [&](const wchar_t* cls, const wchar_t* text, const DWORD style,
                       const int x, const int y, const int width, const int height,
                       const int id) {
            const auto control = CreateWindowExW(
                0, cls, text, WS_CHILD | WS_VISIBLE | style, scaleDip(x, dpi), scaleDip(y, dpi),
                scaleDip(width, dpi), scaleDip(height, dpi),
                dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            state->controlLayouts.push_back({control, x, y, width, height});
            return control;
        };
        add(L"STATIC", state->label.c_str(), 0, 16, 18, 350, 20, 0);
        const auto edit = add(L"EDIT", state->initialValue.c_str(),
                              WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                              16, 44, 350, 25, idValue);
        add(L"BUTTON", L"确定", WS_TABSTOP | BS_DEFPUSHBUTTON,
            196, 88, 80, 28, IDOK);
        add(L"BUTTON", L"取消", WS_TABSTOP,
            286, 88, 80, 28, IDCANCEL);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        refreshSystemAppearance(dialog, *state);
        SetFocus(edit);
        return FALSE;
    }
    if (!state) {
        return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        const int length = GetWindowTextLengthW(GetDlgItem(dialog, idValue));
        std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
        const int copied = GetDlgItemTextW(
            dialog, idValue, value.data(), static_cast<int>(value.size()));
        value.resize(static_cast<std::size_t>(std::max(0, copied)));
        if (value.empty()) {
            showTaskMessage(
                dialog,
                state->title,
                L"名称不能为空。",
                TaskDialogIcon::Warning);
            return TRUE;
        }
        state->result = std::move(value);
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if ((message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) || message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (message == WM_DPICHANGED) {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam); // NOLINT(performance-no-int-to-ptr): WM_DPICHANGED defines LPARAM as RECT*.
        SetWindowPos(
            dialog, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        layoutDialogControls(state->controlLayouts, GetDpiForWindow(dialog));
        refreshSystemAppearance(dialog, *state);
        return TRUE;
    }
    if (isSystemAppearanceMessage(message)) {
        refreshSystemAppearance(dialog, *state);
        return TRUE;
    }
    return FALSE;
    } catch (...) { if (IsWindow(dialog)) EndDialog(dialog, IDCANCEL); return FALSE; }
}

} // namespace

std::optional<std::wstring> showTextPromptDialog(
    const HWND owner,
    const std::wstring_view title,
    const std::wstring_view label,
    const std::wstring_view initialValue)
{
    PromptState state{std::wstring{title}, std::wstring{label},
                      std::wstring{initialValue}};
    const NativeDialogTemplate dialogTemplate{};
    const auto result = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr), dialogTemplate.get(), owner,
        promptProcedure, reinterpret_cast<LPARAM>(&state));
    return result == IDOK ? std::move(state.result) : std::nullopt;
}

} // namespace hlaunch::ui
