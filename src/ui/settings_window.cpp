#include "ui/settings_window.h"

#include "ui/native_dialog_template.h"

#include <algorithm>
#include <cwchar>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr int settingsWidth = 500;
constexpr int settingsHeight = 540;
constexpr int idTheme = 2001;
constexpr int idClose = 2002;
constexpr int idBackdrop = 2003;
constexpr int idOpacity = 2004;
constexpr int idApply = 2005;
constexpr int idHotkeyEnabled = 2010;
constexpr int idHotkeyAlt = 2011;
constexpr int idHotkeyControl = 2012;
constexpr int idHotkeyShift = 2013;
constexpr int idHotkeyWin = 2014;
constexpr int idHotkeyKey = 2015;
constexpr int idScreenEdgeEnabled = 2016;
constexpr int idFullscreenSuppression = 2017;
constexpr int idApplyActivation = 2018;

std::wstring widenAscii(const std::string_view value)
{
    return {value.begin(), value.end()};
}

std::string narrowAscii(const std::wstring_view value)
{
    std::string result{};
    result.reserve(value.size());
    for (const auto character : value) {
        if (character > 0x7F) {
            return {};
        }
        result.push_back(static_cast<char>(character));
    }
    return result;
}

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
    const core::ActivationConfig& activation,
    AppearanceChangedHandler appearanceChangedHandler,
    ActivationChangedHandler activationChangedHandler)
{
    appearance_ = appearance;
    activation_ = activation;
    appearanceChangedHandler_ = std::move(appearanceChangedHandler);
    activationChangedHandler_ = std::move(activationChangedHandler);
    if (!window_ && !create(instance, owner)) {
        return false;
    }
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    setAppearance(appearance_);
    setActivation(activation_);
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

void SettingsWindow::setActivation(const core::ActivationConfig& activation)
{
    activation_ = activation;
    syncActivationControls();
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
        if ((LOWORD(wParam) == idHotkeyEnabled
                || LOWORD(wParam) == idScreenEdgeEnabled)
            && HIWORD(wParam) == BN_CLICKED) {
            updateActivationEnabledState();
            return TRUE;
        }
        if (LOWORD(wParam) == idApplyActivation && HIWORD(wParam) == BN_CLICKED) {
            static_cast<void>(applyActivationFromControls());
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
        hotkeyEnabledCheck_ = nullptr;
        altCheck_ = nullptr;
        controlCheck_ = nullptr;
        shiftCheck_ = nullptr;
        winCheck_ = nullptr;
        hotkeyKeyCombo_ = nullptr;
        screenEdgeEnabledCheck_ = nullptr;
        fullscreenCheck_ = nullptr;
        activationStatusText_ = nullptr;
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

    add(L"BUTTON", L"激活", BS_GROUPBOX, 14, 226, 456, 244, 0);
    hotkeyEnabledCheck_ = add(
        L"BUTTON", L"启用全局快捷键", BS_AUTOCHECKBOX | WS_TABSTOP,
        32, 248, 160, 22, idHotkeyEnabled);
    add(L"STATIC", L"组合键：", 0, 32, 282, 76, 20, 0);
    altCheck_ = add(L"BUTTON", L"Alt", BS_AUTOCHECKBOX | WS_TABSTOP,
        108, 278, 54, 24, idHotkeyAlt);
    controlCheck_ = add(L"BUTTON", L"Ctrl", BS_AUTOCHECKBOX | WS_TABSTOP,
        164, 278, 58, 24, idHotkeyControl);
    shiftCheck_ = add(L"BUTTON", L"Shift", BS_AUTOCHECKBOX | WS_TABSTOP,
        224, 278, 62, 24, idHotkeyShift);
    winCheck_ = add(L"BUTTON", L"Win", BS_AUTOCHECKBOX | WS_TABSTOP,
        288, 278, 56, 24, idHotkeyWin);
    hotkeyKeyCombo_ = add(
        L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        350, 276, 90, 240, idHotkeyKey);
    SendMessageW(hotkeyKeyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Space"));
    for (wchar_t key = L'A'; key <= L'Z'; ++key) {
        const wchar_t value[]{key, L'\0'};
        SendMessageW(hotkeyKeyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    for (wchar_t key = L'0'; key <= L'9'; ++key) {
        const wchar_t value[]{key, L'\0'};
        SendMessageW(hotkeyKeyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    for (int number = 1; number <= 24; ++number) {
        if (number == 12) {
            continue;
        }
        const auto value = L"F" + std::to_wstring(number);
        SendMessageW(hotkeyKeyCombo_, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(value.c_str()));
    }

    screenEdgeEnabledCheck_ = add(
        L"BUTTON", L"启用屏幕边缘停留唤起", BS_AUTOCHECKBOX | WS_TABSTOP,
        32, 316, 210, 22, idScreenEdgeEnabled);
    fullscreenCheck_ = add(
        L"BUTTON", L"全屏应用运行时禁用边缘唤起", BS_AUTOCHECKBOX | WS_TABSTOP,
        52, 346, 240, 22, idFullscreenSuppression);
    add(L"BUTTON", L"应用激活设置", WS_TABSTOP | BS_PUSHBUTTON,
        320, 340, 120, 28, idApplyActivation);
    activationStatusText_ = add(
        L"STATIC", L"修改后按“应用激活设置”；冲突时保留当前有效快捷键。",
        0, 32, 382, 408, 38, 0);
    add(L"STATIC", L"边缘位置、停留时间等高级选项后续提供。",
        0, 32, 430, 408, 20, 0);

    add(L"BUTTON", L"关闭", WS_TABSTOP | BS_PUSHBUTTON,
        390, 486, 80, 28, idClose);
    syncControls();
    syncActivationControls();
}

bool SettingsWindow::applyAppearanceFromControls(const bool includeOpacity)
{
    auto requested = appearance_;
    requested.theme = SendMessageW(themeCombo_, CB_GETCURSEL, 0, 0) == 1
        ? core::ThemeMode::Light
        : core::ThemeMode::Dark;
    switch (SendMessageW(backdropCombo_, CB_GETCURSEL, 0, 0)) {
    case 0:
        requested.backdrop = core::BackdropMode::Solid;
        break;
    case 1:
        requested.backdrop = core::BackdropMode::Mica;
        break;
    case 3:
        requested.backdrop = core::BackdropMode::Tabbed;
        break;
    default:
        requested.backdrop = core::BackdropMode::Acrylic;
        break;
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

bool SettingsWindow::applyActivationFromControls()
{
    auto requested = activation_;
    requested.hotkey.enabled = IsDlgButtonChecked(window_, idHotkeyEnabled) == BST_CHECKED;
    requested.hotkey.modifiers.clear();
    if (IsDlgButtonChecked(window_, idHotkeyAlt) == BST_CHECKED) {
        requested.hotkey.modifiers.push_back(core::HotkeyModifier::Alt);
    }
    if (IsDlgButtonChecked(window_, idHotkeyControl) == BST_CHECKED) {
        requested.hotkey.modifiers.push_back(core::HotkeyModifier::Control);
    }
    if (IsDlgButtonChecked(window_, idHotkeyShift) == BST_CHECKED) {
        requested.hotkey.modifiers.push_back(core::HotkeyModifier::Shift);
    }
    if (IsDlgButtonChecked(window_, idHotkeyWin) == BST_CHECKED) {
        requested.hotkey.modifiers.push_back(core::HotkeyModifier::Win);
    }
    wchar_t key[32]{};
    const auto selected = SendMessageW(hotkeyKeyCombo_, CB_GETCURSEL, 0, 0);
    if (selected != CB_ERR) {
        SendMessageW(hotkeyKeyCombo_, CB_GETLBTEXT, selected,
            reinterpret_cast<LPARAM>(key));
    }
    requested.hotkey.key = narrowAscii(key);
    requested.screenEdge.enabled
        = IsDlgButtonChecked(window_, idScreenEdgeEnabled) == BST_CHECKED;
    requested.screenEdge.disableOnFullscreen
        = IsDlgButtonChecked(window_, idFullscreenSuppression) == BST_CHECKED;

    if (requested.hotkey.modifiers.empty()) {
        SetWindowTextW(activationStatusText_, L"快捷键至少需要一个修饰键。");
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    if (requested == activation_) {
        SetWindowTextW(activationStatusText_, L"当前激活设置已生效。");
        return true;
    }
    if (activationChangedHandler_) {
        const auto result = activationChangedHandler_(requested);
        if (!result) {
            syncActivationControls();
            SetWindowTextW(activationStatusText_, result.error().c_str());
            MessageBeep(MB_ICONWARNING);
            return false;
        }
    }
    activation_ = requested;
    syncActivationControls();
    SetWindowTextW(activationStatusText_, L"激活设置已保存并立即生效。");
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
    case core::BackdropMode::Solid:
        backdropIndex = 0;
        break;
    case core::BackdropMode::Mica:
        backdropIndex = 1;
        break;
    case core::BackdropMode::Acrylic:
        backdropIndex = 2;
        break;
    case core::BackdropMode::Tabbed:
        backdropIndex = 3;
        break;
    }
    SendMessageW(backdropCombo_, CB_SETCURSEL, backdropIndex, 0);
    SetWindowTextW(opacityEdit_, std::to_wstring(appearance_.opacityPercent).c_str());
}

void SettingsWindow::syncActivationControls()
{
    if (!hotkeyEnabledCheck_) {
        return;
    }
    CheckDlgButton(window_, idHotkeyEnabled,
        activation_.hotkey.enabled ? BST_CHECKED : BST_UNCHECKED);
    const auto containsModifier = [this](const core::HotkeyModifier modifier) {
        return std::ranges::find(activation_.hotkey.modifiers, modifier)
            != activation_.hotkey.modifiers.end();
    };
    CheckDlgButton(window_, idHotkeyAlt,
        containsModifier(core::HotkeyModifier::Alt) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window_, idHotkeyControl,
        containsModifier(core::HotkeyModifier::Control) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window_, idHotkeyShift,
        containsModifier(core::HotkeyModifier::Shift) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window_, idHotkeyWin,
        containsModifier(core::HotkeyModifier::Win) ? BST_CHECKED : BST_UNCHECKED);
    const auto key = widenAscii(activation_.hotkey.key);
    auto selected = SendMessageW(hotkeyKeyCombo_, CB_FINDSTRINGEXACT,
        static_cast<WPARAM>(-1),
        reinterpret_cast<LPARAM>(key.c_str()));
    if (selected == CB_ERR) {
        selected = 0;
    }
    SendMessageW(hotkeyKeyCombo_, CB_SETCURSEL, selected, 0);
    CheckDlgButton(window_, idScreenEdgeEnabled,
        activation_.screenEdge.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window_, idFullscreenSuppression,
        activation_.screenEdge.disableOnFullscreen ? BST_CHECKED : BST_UNCHECKED);
    updateActivationEnabledState();
}

void SettingsWindow::updateActivationEnabledState()
{
    const BOOL hotkeyEnabled
        = IsDlgButtonChecked(window_, idHotkeyEnabled) == BST_CHECKED;
    EnableWindow(altCheck_, hotkeyEnabled);
    EnableWindow(controlCheck_, hotkeyEnabled);
    EnableWindow(shiftCheck_, hotkeyEnabled);
    EnableWindow(winCheck_, hotkeyEnabled);
    EnableWindow(hotkeyKeyCombo_, hotkeyEnabled);
    const BOOL edgeEnabled
        = IsDlgButtonChecked(window_, idScreenEdgeEnabled) == BST_CHECKED;
    EnableWindow(fullscreenCheck_, edgeEnabled);
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
