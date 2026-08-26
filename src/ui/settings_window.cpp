#include "ui/settings_window.h"

#include "ui/native_dialog_template.h"

#include <algorithm>
#include <cwchar>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr int settingsWidth = 500;
constexpr int settingsHeight = 390;
constexpr int idTheme = 2001;
constexpr int idClose = 2002;
constexpr int idBackdrop = 2003;
constexpr int idOpacity = 2004;
constexpr int idApply = 2005;

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
    const core::AppearanceConfig& appearance,
    AppearanceChangedHandler appearanceChangedHandler)
{
    appearance_ = appearance;
    appearanceChangedHandler_ = std::move(appearanceChangedHandler);
    if (!window_ && !create(instance, owner)) {
        return false;
    }
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    setAppearance(appearance_);
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

void SettingsWindow::setAppearance(const core::AppearanceConfig& appearance)
{
    appearance_ = appearance;
    syncControls();
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
            static_cast<void>(applyAppearanceFromControls(false));
            return TRUE;
        }
        if (LOWORD(wParam) == idBackdrop && HIWORD(wParam) == CBN_SELCHANGE) {
            static_cast<void>(applyAppearanceFromControls(false));
            return TRUE;
        }
        if (LOWORD(wParam) == idApply && HIWORD(wParam) == BN_CLICKED) {
            static_cast<void>(applyAppearanceFromControls(true));
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
        backdropCombo_ = nullptr;
        opacityEdit_ = nullptr;
        statusText_ = nullptr;
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

    add(L"BUTTON", L"外观", BS_GROUPBOX, 14, 12, 456, 202, 0);
    add(L"STATIC", L"主题：", 0, 32, 46, 88, 20, 0);
    themeCombo_ = add(
        L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        120, 42, 180, 180, idTheme);
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"深色"));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"浅色"));
    add(L"STATIC", L"背景材质：", 0, 32, 84, 88, 20, 0);
    backdropCombo_ = add(
        L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        120, 80, 180, 180, idBackdrop);
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"纯色"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Mica"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Acrylic"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Tabbed"));
    add(L"STATIC", L"不透明度：", 0, 32, 122, 88, 20, 0);
    opacityEdit_ = add(
        L"EDIT", L"95", WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
        120, 118, 72, 24, idOpacity);
    add(L"STATIC", L"%（30–100）", 0, 200, 122, 100, 20, 0);
    add(L"BUTTON", L"应用", WS_TABSTOP | BS_DEFPUSHBUTTON,
        320, 116, 84, 28, idApply);
    statusText_ = add(L"STATIC",
        L"主题和材质选择会立即保存；输入不透明度后按“应用”。",
        0, 32, 154, 400, 20, 0);
    add(L"STATIC",
        L"外观同时作用于主窗口和底部搜索窗；系统对话框跟随 Windows 外观。",
        0, 32, 180, 408, 20, 0);

    add(L"BUTTON", L"激活", BS_GROUPBOX, 14, 226, 456, 72, 0);
    add(L"STATIC", L"快捷键和边缘唤起将在后续选项页中提供。",
        0, 32, 256, 408, 20, 0);

    add(L"BUTTON", L"关闭", WS_TABSTOP | BS_PUSHBUTTON,
        390, 312, 80, 28, idClose);
    syncControls();
}

bool SettingsWindow::applyAppearanceFromControls(const bool includeOpacity)
{
    auto requested = appearance_;
    requested.theme = SendMessageW(themeCombo_, CB_GETCURSEL, 0, 0) == 1
        ? core::ThemeMode::Light
        : core::ThemeMode::Dark;
    switch (SendMessageW(backdropCombo_, CB_GETCURSEL, 0, 0)) {
    case 0: requested.backdrop = core::BackdropMode::Solid; break;
    case 1: requested.backdrop = core::BackdropMode::Mica; break;
    case 3: requested.backdrop = core::BackdropMode::Tabbed; break;
    default: requested.backdrop = core::BackdropMode::Acrylic; break;
    }
    if (includeOpacity) {
        wchar_t buffer[8]{};
        GetWindowTextW(opacityEdit_, buffer, static_cast<int>(std::size(buffer)));
        wchar_t* end{};
        const auto value = std::wcstoul(buffer, &end, 10);
        if (buffer[0] == L'\0' || !end || *end != L'\0' || value < 30 || value > 100) {
            SetWindowTextW(statusText_, L"请输入 30 到 100 之间的整数。");
            MessageBeep(MB_ICONWARNING);
            SetFocus(opacityEdit_);
            SendMessageW(opacityEdit_, EM_SETSEL, 0, -1);
            return false;
        }
        requested.opacityPercent = static_cast<std::uint8_t>(value);
    }
    if (requested == appearance_) {
        SetWindowTextW(statusText_, L"当前外观设置已生效。");
        return true;
    }
    if (appearanceChangedHandler_ && !appearanceChangedHandler_(requested)) {
        syncControls();
        SetWindowTextW(statusText_, L"保存失败，已恢复原设置。");
        return false;
    }
    appearance_ = requested;
    syncControls();
    SetWindowTextW(statusText_, L"外观设置已保存并立即生效。");
    return true;
}

void SettingsWindow::syncControls()
{
    if (!themeCombo_) {
        return;
    }
    SendMessageW(themeCombo_, CB_SETCURSEL,
        appearance_.theme == core::ThemeMode::Light ? 1 : 0, 0);
    int backdropIndex = 2;
    switch (appearance_.backdrop) {
    case core::BackdropMode::Solid: backdropIndex = 0; break;
    case core::BackdropMode::Mica: backdropIndex = 1; break;
    case core::BackdropMode::Acrylic: backdropIndex = 2; break;
    case core::BackdropMode::Tabbed: backdropIndex = 3; break;
    }
    SendMessageW(backdropCombo_, CB_SETCURSEL, backdropIndex, 0);
    SetWindowTextW(opacityEdit_, std::to_wstring(appearance_.opacityPercent).c_str());
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
