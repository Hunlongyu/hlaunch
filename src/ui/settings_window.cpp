#include "ui/settings_window.h"

#include "ui/native_dialog_template.h"

#include <algorithm>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr int settingsWidth = 500;
constexpr int settingsHeight = 330;
constexpr int idTheme = 2001;
constexpr int idClose = 2002;

} // namespace

SettingsWindow::~SettingsWindow()
{
    if (window_) {
        DestroyWindow(window_);
    }
}

bool SettingsWindow::show(
    const HINSTANCE instance,
    const HWND owner,
    const core::ThemeMode themeMode,
    ThemeChangedHandler themeChangedHandler)
{
    themeMode_ = themeMode;
    themeChangedHandler_ = std::move(themeChangedHandler);
    if (!window_ && !create(instance, owner)) {
        return false;
    }
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    setThemeMode(themeMode_);
    positionOverOwner(owner);
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    SetFocus(themeCombo_);
    return true;
}

void SettingsWindow::hide()
{
    if (window_) {
        ShowWindow(window_, SW_HIDE);
    }
}

void SettingsWindow::setThemeMode(const core::ThemeMode themeMode)
{
    themeMode_ = themeMode;
    if (themeCombo_) {
        SendMessageW(themeCombo_, CB_SETCURSEL,
                     themeMode == core::ThemeMode::Light ? 1 : 0, 0);
    }
}

HWND SettingsWindow::handle() const noexcept
{
    return window_;
}

bool SettingsWindow::isVisible() const noexcept
{
    return window_ && IsWindowVisible(window_);
}

INT_PTR CALLBACK SettingsWindow::dialogProcedure(
    const HWND dialog,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        self = reinterpret_cast<SettingsWindow*>(lParam);
        self->window_ = dialog;
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handleMessage(message, wParam, lParam) : FALSE;
}

INT_PTR SettingsWindow::handleMessage(
    const UINT message,
    const WPARAM wParam,
    const LPARAM)
{
    switch (message) {
    case WM_INITDIALOG:
        SetWindowTextW(window_, L"HLaunch 选项");
        SetWindowPos(window_, nullptr, 0, 0, settingsWidth, settingsHeight,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
        createControls();
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == idTheme && HIWORD(wParam) == CBN_SELCHANGE) {
            const auto selected = SendMessageW(themeCombo_, CB_GETCURSEL, 0, 0);
            const auto requested = selected == 1
                ? core::ThemeMode::Light
                : core::ThemeMode::Dark;
            if (requested != themeMode_
                && (!themeChangedHandler_ || !themeChangedHandler_(requested))) {
                SendMessageW(themeCombo_, CB_SETCURSEL,
                             themeMode_ == core::ThemeMode::Light ? 1 : 0, 0);
            }
            else {
                themeMode_ = requested;
            }
            return TRUE;
        }
        if (LOWORD(wParam) == idClose || LOWORD(wParam) == IDCANCEL) {
            hide();
            return TRUE;
        }
        return FALSE;
    case WM_CLOSE:
        hide();
        return TRUE;
    case WM_DESTROY:
        window_ = nullptr;
        themeCombo_ = nullptr;
        return TRUE;
    default:
        return FALSE;
    }
}

bool SettingsWindow::create(const HINSTANCE instance, const HWND owner)
{
    const NativeDialogTemplate dialogTemplate{};
    window_ = CreateDialogIndirectParamW(
        instance, dialogTemplate.get(), owner, dialogProcedure,
        reinterpret_cast<LPARAM>(this));
    return window_ != nullptr;
}

void SettingsWindow::createControls()
{
    const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto add = [&](const wchar_t* cls, const wchar_t* text, const DWORD style,
                   const int x, const int y, const int width, const int height,
                   const int id) {
        const auto control = CreateWindowExW(
            0, cls, text, WS_CHILD | WS_VISIBLE | style,
            x, y, width, height, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    };

    add(L"BUTTON", L"外观", BS_GROUPBOX, 14, 12, 456, 116, 0);
    add(L"STATIC", L"主题：", 0, 32, 48, 88, 20, 0);
    themeCombo_ = add(
        L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        120, 44, 180, 180, idTheme);
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"深色"));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"浅色"));
    SendMessageW(themeCombo_, CB_SETCURSEL,
                 themeMode_ == core::ThemeMode::Light ? 1 : 0, 0);
    add(L"STATIC",
        L"主题作用于 Launcher、Grid、Tab 和搜索框；系统对话框跟随 Windows 外观。",
        0, 32, 78, 408, 34, 0);

    add(L"BUTTON", L"激活", BS_GROUPBOX, 14, 140, 456, 82, 0);
    add(L"STATIC", L"快捷键和边缘唤起将在后续选项页中提供。",
        0, 32, 172, 408, 20, 0);

    add(L"BUTTON", L"关闭", WS_TABSTOP | BS_DEFPUSHBUTTON,
        390, 244, 80, 28, idClose);
}

void SettingsWindow::positionOverOwner(const HWND owner)
{
    RECT ownerBounds{};
    RECT dialogBounds{};
    GetWindowRect(owner, &ownerBounds);
    GetWindowRect(window_, &dialogBounds);
    const int width = dialogBounds.right - dialogBounds.left;
    const int height = dialogBounds.bottom - dialogBounds.top;
    const int x = ownerBounds.left
        + std::max<LONG>(0, (ownerBounds.right - ownerBounds.left - width) / 2);
    const int y = ownerBounds.top
        + std::max<LONG>(0, (ownerBounds.bottom - ownerBounds.top - height) / 2);
    SetWindowPos(window_, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

} // namespace hlaunch::ui
