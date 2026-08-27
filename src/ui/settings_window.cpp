#include "ui/settings_window.h"

#include "ui/native_dialog_template.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr int settingsWidth = 500;
constexpr int settingsHeight = 750;
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
constexpr int idEdgeLeft = 2019;
constexpr int idEdgeRight = 2020;
constexpr int idEdgeTop = 2021;
constexpr int idEdgeBottom = 2022;
constexpr int idEdgeTopLeft = 2023;
constexpr int idEdgeTopRight = 2024;
constexpr int idEdgeBottomLeft = 2025;
constexpr int idEdgeBottomRight = 2026;
constexpr int idEdgeMode = 2027;
constexpr int idEdgeThickness = 2028;
constexpr int idEdgeCornerSize = 2029;
constexpr int idEdgeDwell = 2030;
constexpr int idEdgePoll = 2031;
constexpr int idEdgeCooldown = 2032;
constexpr int idStartupEnabled = 2040;
constexpr int idApplyStartup = 2041;

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

std::optional<double> readDouble(const HWND control)
{
    wchar_t buffer[32]{};
    GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer)));
    wchar_t* end{};
    const auto value = std::wcstod(buffer, &end);
    if (buffer[0] == L'\0' || !end || *end != L'\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint32_t> readUnsigned(const HWND control)
{
    wchar_t buffer[32]{};
    GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer)));
    wchar_t* end{};
    const auto value = std::wcstoul(buffer, &end, 10);
    if (buffer[0] == L'\0' || !end || *end != L'\0'
        || value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

std::wstring formatDecimal(const double value)
{
    std::wostringstream stream{};
    stream << std::setprecision(4) << value;
    return stream.str();
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
    std::expected<bool, std::wstring> startupEnabled,
    AppearanceChangedHandler appearanceChangedHandler,
    ActivationChangedHandler activationChangedHandler,
    StartupChangedHandler startupChangedHandler)
{
    appearance_ = appearance;
    activation_ = activation;
    appearanceChangedHandler_ = std::move(appearanceChangedHandler);
    activationChangedHandler_ = std::move(activationChangedHandler);
    startupChangedHandler_ = std::move(startupChangedHandler);
    if (startupEnabled) {
        startupEnabled_ = *startupEnabled;
        startupLoadError_.clear();
    }
    else {
        startupEnabled_ = false;
        startupLoadError_ = std::move(startupEnabled.error());
    }
    if (!window_ && !create(instance, owner)) {
        return false;
    }
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    setAppearance(appearance_);
    setActivation(activation_);
    syncStartupControls();
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
        if (LOWORD(wParam) == idApplyStartup && HIWORD(wParam) == BN_CLICKED) {
            static_cast<void>(applyStartupFromControls());
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
        leftZoneCheck_ = nullptr;
        rightZoneCheck_ = nullptr;
        topZoneCheck_ = nullptr;
        bottomZoneCheck_ = nullptr;
        topLeftZoneCheck_ = nullptr;
        topRightZoneCheck_ = nullptr;
        bottomLeftZoneCheck_ = nullptr;
        bottomRightZoneCheck_ = nullptr;
        edgeModeCombo_ = nullptr;
        thicknessEdit_ = nullptr;
        cornerSizeEdit_ = nullptr;
        dwellEdit_ = nullptr;
        pollEdit_ = nullptr;
        cooldownEdit_ = nullptr;
        fullscreenCheck_ = nullptr;
        activationStatusText_ = nullptr;
        startupEnabledCheck_ = nullptr;
        startupApplyButton_ = nullptr;
        startupStatusText_ = nullptr;
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

    add(L"BUTTON", L"外观", BS_GROUPBOX, 14, 12, 456, 174, 0);
    add(L"STATIC", L"主题：", 0, 32, 42, 88, 20, 0);
    themeCombo_ = add(
        L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        120, 38, 180, 180, idTheme);
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"深色"));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"浅色"));
    add(L"STATIC", L"背景材质：", 0, 32, 78, 88, 20, 0);
    backdropCombo_ = add(
        L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        120, 74, 180, 180, idBackdrop);
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"纯色"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Mica"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Acrylic"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Tabbed"));
    add(L"STATIC", L"不透明度：", 0, 32, 114, 88, 20, 0);
    opacityEdit_ = add(
        L"EDIT", L"95", WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
        120, 110, 72, 24, idOpacity);
    add(L"STATIC", L"%（30–100）", 0, 200, 114, 100, 20, 0);
    add(L"BUTTON", L"应用", WS_TABSTOP | BS_DEFPUSHBUTTON,
        320, 108, 84, 28, idApply);
    statusText_ = add(L"STATIC",
        L"主题和材质会立即保存；不透明度同时作用于主窗口和搜索窗。",
        0, 32, 148, 408, 28, 0);

    add(L"BUTTON", L"激活", BS_GROUPBOX, 14, 198, 456, 382, 0);
    hotkeyEnabledCheck_ = add(
        L"BUTTON", L"启用全局快捷键", BS_AUTOCHECKBOX | WS_TABSTOP,
        32, 220, 160, 22, idHotkeyEnabled);
    add(L"STATIC", L"组合键：", 0, 32, 254, 76, 20, 0);
    altCheck_ = add(L"BUTTON", L"Alt", BS_AUTOCHECKBOX | WS_TABSTOP,
        108, 250, 54, 24, idHotkeyAlt);
    controlCheck_ = add(L"BUTTON", L"Ctrl", BS_AUTOCHECKBOX | WS_TABSTOP,
        164, 250, 58, 24, idHotkeyControl);
    shiftCheck_ = add(L"BUTTON", L"Shift", BS_AUTOCHECKBOX | WS_TABSTOP,
        224, 250, 62, 24, idHotkeyShift);
    winCheck_ = add(L"BUTTON", L"Win", BS_AUTOCHECKBOX | WS_TABSTOP,
        288, 250, 56, 24, idHotkeyWin);
    hotkeyKeyCombo_ = add(
        L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        350, 248, 90, 240, idHotkeyKey);
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
        32, 286, 210, 22, idScreenEdgeEnabled);
    add(L"STATIC", L"热区：", 0, 32, 320, 44, 20, 0);
    leftZoneCheck_ = add(L"BUTTON", L"左", BS_AUTOCHECKBOX | WS_TABSTOP,
        76, 316, 42, 24, idEdgeLeft);
    rightZoneCheck_ = add(L"BUTTON", L"右", BS_AUTOCHECKBOX | WS_TABSTOP,
        120, 316, 42, 24, idEdgeRight);
    topZoneCheck_ = add(L"BUTTON", L"上", BS_AUTOCHECKBOX | WS_TABSTOP,
        164, 316, 42, 24, idEdgeTop);
    bottomZoneCheck_ = add(L"BUTTON", L"下", BS_AUTOCHECKBOX | WS_TABSTOP,
        208, 316, 42, 24, idEdgeBottom);
    topLeftZoneCheck_ = add(L"BUTTON", L"左上", BS_AUTOCHECKBOX | WS_TABSTOP,
        252, 316, 52, 24, idEdgeTopLeft);
    topRightZoneCheck_ = add(L"BUTTON", L"右上", BS_AUTOCHECKBOX | WS_TABSTOP,
        306, 316, 52, 24, idEdgeTopRight);
    bottomLeftZoneCheck_ = add(L"BUTTON", L"左下", BS_AUTOCHECKBOX | WS_TABSTOP,
        360, 316, 52, 24, idEdgeBottomLeft);
    bottomRightZoneCheck_ = add(L"BUTTON", L"右下", BS_AUTOCHECKBOX | WS_TABSTOP,
        414, 316, 52, 24, idEdgeBottomRight);

    add(L"STATIC", L"多显示器：", 0, 32, 352, 76, 20, 0);
    edgeModeCombo_ = add(
        L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        108, 348, 196, 120, idEdgeMode);
    SendMessageW(edgeModeCombo_, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"仅桌面外轮廓"));
    SendMessageW(edgeModeCombo_, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"每台显示器边缘"));

    add(L"STATIC", L"边缘宽度：", 0, 32, 386, 76, 20, 0);
    thicknessEdit_ = add(L"EDIT", L"4", WS_BORDER | WS_TABSTOP | ES_RIGHT,
        108, 382, 54, 24, idEdgeThickness);
    add(L"STATIC", L"DIP（2–16）", 0, 168, 386, 88, 20, 0);
    add(L"STATIC", L"角落大小：", 0, 264, 386, 76, 20, 0);
    cornerSizeEdit_ = add(L"EDIT", L"16", WS_BORDER | WS_TABSTOP | ES_RIGHT,
        340, 382, 52, 24, idEdgeCornerSize);
    add(L"STATIC", L"DIP", 0, 398, 386, 34, 20, 0);

    add(L"STATIC", L"停留：", 0, 32, 420, 48, 20, 0);
    dwellEdit_ = add(L"EDIT", L"300", WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
        80, 416, 54, 24, idEdgeDwell);
    add(L"STATIC", L"ms", 0, 138, 420, 24, 20, 0);
    add(L"STATIC", L"采样：", 0, 170, 420, 48, 20, 0);
    pollEdit_ = add(L"EDIT", L"40", WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
        218, 416, 48, 24, idEdgePoll);
    add(L"STATIC", L"ms", 0, 270, 420, 24, 20, 0);
    add(L"STATIC", L"冷却：", 0, 302, 420, 48, 20, 0);
    cooldownEdit_ = add(L"EDIT", L"500", WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
        350, 416, 54, 24, idEdgeCooldown);
    add(L"STATIC", L"ms", 0, 408, 420, 24, 20, 0);

    fullscreenCheck_ = add(
        L"BUTTON", L"全屏应用运行时禁用边缘唤起", BS_AUTOCHECKBOX | WS_TABSTOP,
        52, 448, 240, 22, idFullscreenSuppression);
    add(L"BUTTON", L"应用激活设置", WS_TABSTOP | BS_PUSHBUTTON,
        320, 476, 120, 28, idApplyActivation);
    activationStatusText_ = add(
        L"STATIC", L"修改后应用；快捷键冲突或边缘服务失败时恢复原设置。",
        0, 32, 518, 408, 42, 0);

    add(L"BUTTON", L"Windows 登录", BS_GROUPBOX, 14, 586, 456, 80, 0);
    startupEnabledCheck_ = add(
        L"BUTTON", L"登录 Windows 时启动 HLaunch", BS_AUTOCHECKBOX | WS_TABSTOP,
        32, 606, 244, 24, idStartupEnabled);
    startupApplyButton_ = add(L"BUTTON", L"应用开机设置", WS_TABSTOP | BS_PUSHBUTTON,
        320, 602, 120, 28, idApplyStartup);
    startupStatusText_ = add(
        L"STATIC", L"仅修改当前用户启动项，不需要管理员权限。",
        0, 32, 636, 408, 22, 0);

    add(L"BUTTON", L"关闭", WS_TABSTOP | BS_PUSHBUTTON,
        390, 674, 80, 28, idClose);
    syncControls();
    syncActivationControls();
    syncStartupControls();
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
    requested.screenEdge.zones.clear();
    const auto addZone = [&](const int id, const core::ScreenEdgeZone zone) {
        if (IsDlgButtonChecked(window_, id) == BST_CHECKED) {
            requested.screenEdge.zones.push_back(zone);
        }
    };
    addZone(idEdgeLeft, core::ScreenEdgeZone::Left);
    addZone(idEdgeRight, core::ScreenEdgeZone::Right);
    addZone(idEdgeTop, core::ScreenEdgeZone::Top);
    addZone(idEdgeBottom, core::ScreenEdgeZone::Bottom);
    addZone(idEdgeTopLeft, core::ScreenEdgeZone::TopLeft);
    addZone(idEdgeTopRight, core::ScreenEdgeZone::TopRight);
    addZone(idEdgeBottomLeft, core::ScreenEdgeZone::BottomLeft);
    addZone(idEdgeBottomRight, core::ScreenEdgeZone::BottomRight);
    requested.screenEdge.edgeMode = SendMessageW(edgeModeCombo_, CB_GETCURSEL, 0, 0) == 1
        ? core::ScreenEdgeMode::EveryMonitor
        : core::ScreenEdgeMode::DesktopOuter;

    const auto rejectField = [this](const HWND field, const wchar_t* message) {
        SetWindowTextW(activationStatusText_, message);
        MessageBeep(MB_ICONWARNING);
        SetFocus(field);
        SendMessageW(field, EM_SETSEL, 0, -1);
        return false;
    };
    const auto thickness = readDouble(thicknessEdit_);
    if (!thickness || *thickness < 2.0 || *thickness > 16.0) {
        return rejectField(thicknessEdit_, L"边缘宽度必须是 2 到 16 DIP。");
    }
    requested.screenEdge.thicknessDip = *thickness;
    const auto cornerSize = readDouble(cornerSizeEdit_);
    if (!cornerSize || *cornerSize < 8.0 || *cornerSize > 64.0) {
        return rejectField(cornerSizeEdit_, L"角落大小必须是 8 到 64 DIP。");
    }
    requested.screenEdge.cornerSizeDip = *cornerSize;
    const auto dwell = readUnsigned(dwellEdit_);
    if (!dwell || *dwell < 100 || *dwell > 1'000) {
        return rejectField(dwellEdit_, L"停留时间必须是 100 到 1000 毫秒。");
    }
    requested.screenEdge.dwellMs = *dwell;
    const auto poll = readUnsigned(pollEdit_);
    if (!poll || *poll < 30 || *poll > 50) {
        return rejectField(pollEdit_, L"采样间隔必须是 30 到 50 毫秒。");
    }
    requested.screenEdge.pollMs = *poll;
    const auto cooldown = readUnsigned(cooldownEdit_);
    if (!cooldown || *cooldown > 5'000) {
        return rejectField(cooldownEdit_, L"冷却时间必须是 0 到 5000 毫秒。");
    }
    requested.screenEdge.cooldownMs = *cooldown;

    if (requested.hotkey.modifiers.empty()) {
        SetWindowTextW(activationStatusText_, L"快捷键至少需要一个修饰键。");
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    if (requested.screenEdge.zones.empty()) {
        SetWindowTextW(activationStatusText_, L"至少选择一个屏幕边缘热区。");
        MessageBeep(MB_ICONWARNING);
        SetFocus(leftZoneCheck_);
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

bool SettingsWindow::applyStartupFromControls()
{
    const bool requested
        = IsDlgButtonChecked(window_, idStartupEnabled) == BST_CHECKED;
    if (startupChangedHandler_) {
        const auto result = startupChangedHandler_(requested);
        if (!result) {
            syncStartupControls();
            SetWindowTextW(startupStatusText_, result.error().c_str());
            MessageBeep(MB_ICONWARNING);
            return false;
        }
    }
    startupEnabled_ = requested;
    startupLoadError_.clear();
    syncStartupControls();
    SetWindowTextW(
        startupStatusText_,
        startupEnabled_ ? L"已启用当前用户开机启动。" : L"已关闭当前用户开机启动。");
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
    const auto containsZone = [this](const core::ScreenEdgeZone zone) {
        return std::ranges::find(activation_.screenEdge.zones, zone)
            != activation_.screenEdge.zones.end();
    };
    const auto syncZone = [&](const int id, const core::ScreenEdgeZone zone) {
        CheckDlgButton(window_, id, containsZone(zone) ? BST_CHECKED : BST_UNCHECKED);
    };
    syncZone(idEdgeLeft, core::ScreenEdgeZone::Left);
    syncZone(idEdgeRight, core::ScreenEdgeZone::Right);
    syncZone(idEdgeTop, core::ScreenEdgeZone::Top);
    syncZone(idEdgeBottom, core::ScreenEdgeZone::Bottom);
    syncZone(idEdgeTopLeft, core::ScreenEdgeZone::TopLeft);
    syncZone(idEdgeTopRight, core::ScreenEdgeZone::TopRight);
    syncZone(idEdgeBottomLeft, core::ScreenEdgeZone::BottomLeft);
    syncZone(idEdgeBottomRight, core::ScreenEdgeZone::BottomRight);
    SendMessageW(edgeModeCombo_, CB_SETCURSEL,
        activation_.screenEdge.edgeMode == core::ScreenEdgeMode::EveryMonitor ? 1 : 0, 0);
    SetWindowTextW(
        thicknessEdit_, formatDecimal(activation_.screenEdge.thicknessDip).c_str());
    SetWindowTextW(
        cornerSizeEdit_, formatDecimal(activation_.screenEdge.cornerSizeDip).c_str());
    SetWindowTextW(dwellEdit_, std::to_wstring(activation_.screenEdge.dwellMs).c_str());
    SetWindowTextW(pollEdit_, std::to_wstring(activation_.screenEdge.pollMs).c_str());
    SetWindowTextW(
        cooldownEdit_, std::to_wstring(activation_.screenEdge.cooldownMs).c_str());
    CheckDlgButton(window_, idFullscreenSuppression,
        activation_.screenEdge.disableOnFullscreen ? BST_CHECKED : BST_UNCHECKED);
    updateActivationEnabledState();
}

void SettingsWindow::syncStartupControls()
{
    if (!startupEnabledCheck_) {
        return;
    }
    CheckDlgButton(
        window_, idStartupEnabled, startupEnabled_ ? BST_CHECKED : BST_UNCHECKED);
    const BOOL available = startupLoadError_.empty();
    EnableWindow(startupEnabledCheck_, available);
    EnableWindow(startupApplyButton_, available);
    if (!available) {
        SetWindowTextW(startupStatusText_, startupLoadError_.c_str());
    }
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
    EnableWindow(leftZoneCheck_, edgeEnabled);
    EnableWindow(rightZoneCheck_, edgeEnabled);
    EnableWindow(topZoneCheck_, edgeEnabled);
    EnableWindow(bottomZoneCheck_, edgeEnabled);
    EnableWindow(topLeftZoneCheck_, edgeEnabled);
    EnableWindow(topRightZoneCheck_, edgeEnabled);
    EnableWindow(bottomLeftZoneCheck_, edgeEnabled);
    EnableWindow(bottomRightZoneCheck_, edgeEnabled);
    EnableWindow(edgeModeCombo_, edgeEnabled);
    EnableWindow(thicknessEdit_, edgeEnabled);
    EnableWindow(cornerSizeEdit_, edgeEnabled);
    EnableWindow(dwellEdit_, edgeEnabled);
    EnableWindow(pollEdit_, edgeEnabled);
    EnableWindow(cooldownEdit_, edgeEnabled);
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
    const int desiredX = ownerBounds.left
        + std::max<LONG>(0, (ownerBounds.right - ownerBounds.left - width) / 2);
    const int desiredY = ownerBounds.top
        + std::max<LONG>(0, (ownerBounds.bottom - ownerBounds.top - height) / 2);
    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
    const auto monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        SetWindowPos(window_, nullptr, desiredX, desiredY, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        return;
    }
    const int workLeft = monitorInfo.rcWork.left;
    const int workTop = monitorInfo.rcWork.top;
    const int workRight = monitorInfo.rcWork.right;
    const int workBottom = monitorInfo.rcWork.bottom;
    const int maximumX = std::max(workLeft, workRight - width);
    const int maximumY = std::max(workTop, workBottom - height);
    const int x = std::clamp(desiredX, workLeft, maximumX);
    const int y = std::clamp(desiredY, workTop, maximumY);
    SetWindowPos(window_, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

} // namespace hlaunch::ui
