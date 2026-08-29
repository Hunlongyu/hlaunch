#include "ui/settings_window.h"

#include "ui/native_dialog_template.h"
#include "ui/visual_style.h"

#include <CommCtrl.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr int settingsWidth = 560;
constexpr int settingsHeight = 590;
constexpr int compactEditHeightDip = 22;
constexpr int compactComboSelectionHeightDip = 16;
constexpr int idTab = 2000;
constexpr int idBackdrop = 2001;
constexpr int idTabPageBackground = 2002;
constexpr int idApplyAll = 2003;
constexpr int idOpacitySlider = 2004;
constexpr int idOpacityEdit = 2005;
constexpr int idHotkeyEnabled = 2010;
constexpr int idHotkeyAlt = 2011;
constexpr int idHotkeyControl = 2012;
constexpr int idHotkeyShift = 2013;
constexpr int idHotkeyWin = 2014;
constexpr int idHotkeyKey = 2015;
constexpr int idScreenEdgeEnabled = 2016;
constexpr int idFullscreenSuppression = 2017;
constexpr int idEdgeLeft = 2019;
constexpr int idEdgeRight = 2020;
constexpr int idEdgeTop = 2021;
constexpr int idEdgeBottom = 2022;
constexpr int idEdgeTopLeft = 2023;
constexpr int idEdgeTopRight = 2024;
constexpr int idEdgeBottomLeft = 2025;
constexpr int idEdgeBottomRight = 2026;
constexpr int idEdgeMode = 2027;
constexpr int idProcessBlocklist = 2033;
constexpr int idProcessAllowlist = 2034;
constexpr int idDiagnosticLoggingEnabled = 2050;

std::wstring widenAscii(const std::string_view value)
{
    return {value.begin(), value.end()};
}

std::string narrowAscii(const std::wstring_view value)
{
    std::string result{};
    result.reserve(value.size());
    for (const auto character : value) {
        if (character > 0x7F) return {};
        result.push_back(static_cast<char>(character));
    }
    return result;
}

std::optional<std::uint32_t> readUnsigned(const HWND control)
{
    wchar_t buffer[32]{};
    GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer)));
    wchar_t* end{};
    const auto value = std::wcstoul(buffer, &end, 10);
    if (buffer[0] == L'\0' || !end || *end != L'\0'
        || value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

std::wstring readText(const HWND control)
{
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }
    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(control, value.data(), length + 1);
    value.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0U);
    return value;
}

std::optional<std::string> narrowUtf8(const std::wstring_view value)
{
    if (value.empty() || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const int sourceLength = static_cast<int>(value.size());
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), sourceLength, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return std::nullopt;
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), sourceLength,
            result.data(), required, nullptr, nullptr) != required) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::wstring> widenUtf8(const std::string_view value)
{
    if (value.empty() || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const int sourceLength = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), sourceLength, nullptr, 0);
    if (required <= 0) {
        return std::nullopt;
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), sourceLength,
            result.data(), required) != required) {
        return std::nullopt;
    }
    return result;
}

bool endsWithExe(const std::wstring_view value) noexcept
{
    return value.size() >= 4
        && CompareStringOrdinal(
               value.data() + value.size() - 4, 4, L".exe", 4, TRUE) == CSTR_EQUAL;
}

std::expected<std::vector<std::string>, std::wstring> parseProcessList(const HWND control)
{
    auto input = readText(control);
    for (auto& character : input) {
        if (character == L',' || character == L'\r' || character == L'\n') {
            character = L';';
        }
    }

    std::vector<std::string> names{};
    std::size_t start{};
    while (start <= input.size()) {
        const auto separator = input.find(L';', start);
        const auto end = separator == std::wstring::npos ? input.size() : separator;
        auto token = input.substr(start, end - start);
        const auto first = token.find_first_not_of(L" \t");
        if (first != std::wstring::npos) {
            const auto last = token.find_last_not_of(L" \t");
            token = token.substr(first, last - first + 1);
            if (token.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
                return std::unexpected(L"进程名单只能填写可执行文件名，不能包含路径或通配符。");
            }
            if (!endsWithExe(token)) {
                token += L".exe";
            }
            if (token.size() > 260) {
                return std::unexpected(L"进程文件名不能超过 260 个字符。");
            }
            CharLowerBuffW(token.data(), static_cast<DWORD>(token.size()));
            const auto encoded = narrowUtf8(token);
            if (!encoded) {
                return std::unexpected(L"进程文件名包含无法保存的字符。");
            }
            if (std::ranges::find(names, *encoded) == names.end()) {
                names.push_back(*encoded);
            }
            if (names.size() > 64) {
                return std::unexpected(L"每个进程名单最多允许 64 项。");
            }
        }
        if (separator == std::wstring::npos) {
            break;
        }
        start = separator + 1;
    }
    return names;
}

std::wstring formatProcessList(const std::vector<std::string>& names)
{
    std::wstring result{};
    for (const auto& name : names) {
        const auto widened = widenUtf8(name);
        if (!widened) {
            continue;
        }
        if (!result.empty()) {
            result += L"\r\n";
        }
        result += *widened;
    }
    return result;
}

} // namespace

SettingsWindow::~SettingsWindow()
{
    if (window_) DestroyWindow(window_);
}

bool SettingsWindow::show(
    const HINSTANCE instance,
    const HWND owner,
    const core::AppearanceConfig& appearance,
    const core::ActivationConfig& activation,
    const bool diagnosticLoggingEnabled,
    AppearanceChangedHandler appearanceChangedHandler,
    ActivationChangedHandler activationChangedHandler,
    DiagnosticsChangedHandler diagnosticsChangedHandler)
{
    appearance_ = appearance;
    activation_ = activation;
    diagnosticLoggingEnabled_ = diagnosticLoggingEnabled;
    appearanceChangedHandler_ = std::move(appearanceChangedHandler);
    activationChangedHandler_ = std::move(activationChangedHandler);
    diagnosticsChangedHandler_ = std::move(diagnosticsChangedHandler);
    if (!window_ && !create(instance, owner)) return false;
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    setAppearance(appearance_);
    setActivation(activation_);
    syncDiagnosticsControls();
    SetWindowTextW(settingsStatusText_, L"修改后选择“应用”或“确定”保存设置。");
    positionOverOwner(owner);
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    SetFocus(tabControl_);
    return true;
}

void SettingsWindow::hide()
{
    if (window_) ShowWindow(window_, SW_HIDE);
}

void SettingsWindow::setAppearance(const core::AppearanceConfig& appearance)
{
    appearance_ = appearance;
    syncAppearanceControls();
}

void SettingsWindow::setActivation(const core::ActivationConfig& activation)
{
    activation_ = activation;
    syncActivationControls();
}

void SettingsWindow::setActivationStatus(const wchar_t* const message)
{
    if (activationStatusText_ && message) {
        SetWindowTextW(activationStatusText_, message);
    }
}

void SettingsWindow::setStatus(const wchar_t* const message)
{
    if (settingsStatusText_ && message) {
        SetWindowTextW(settingsStatusText_, message);
    }
}

void SettingsWindow::setDiagnosticLogging(
    const bool enabled,
    const wchar_t* const status)
{
    diagnosticLoggingEnabled_ = enabled;
    syncDiagnosticsControls();
    if (diagnosticsStatusText_ && status) {
        SetWindowTextW(diagnosticsStatusText_, status);
    }
}

void SettingsWindow::refreshSystemAppearance()
{
    if (!window_) return;
    static_cast<void>(systemUiFont_.refresh(GetDpiForWindow(window_)));
    applySystemUiFont(window_, systemUiFont_.get());
    applyNativeWindowStyle(window_, false);
    EnumChildWindows(window_, [](const HWND child, const LPARAM) noexcept -> BOOL {
        applyNativeControlStyle(child, false);
        return TRUE;
    }, 0);
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

HWND SettingsWindow::handle() const noexcept { return window_; }
bool SettingsWindow::isVisible() const noexcept { return window_ && IsWindowVisible(window_); }

INT_PTR CALLBACK SettingsWindow::dialogProcedure(
    const HWND dialog, const UINT message, const WPARAM wParam, const LPARAM lParam) noexcept
{
    try {
        auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (message == WM_INITDIALOG) {
            self = reinterpret_cast<SettingsWindow*>(lParam);
            self->window_ = dialog;
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->handleMessage(message, wParam, lParam) : FALSE;
    } catch (...) {
        if (IsWindow(dialog)) DestroyWindow(dialog);
        return FALSE;
    }
}

INT_PTR SettingsWindow::handleMessage(
    const UINT message, const WPARAM wParam, const LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG: {
        SetWindowTextW(window_, L"HLaunch 设置");
        const UINT initialDpi = GetDpiForWindow(window_);
        RECT windowBounds{
            0, 0, scaleDip(settingsWidth, initialDpi), scaleDip(settingsHeight, initialDpi)};
        AdjustWindowRectExForDpi(
            &windowBounds,
            static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)),
            FALSE,
            static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)),
            initialDpi);
        SetWindowPos(window_, nullptr, 0, 0,
                     windowBounds.right - windowBounds.left,
                     windowBounds.bottom - windowBounds.top,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
        createControls();
        refreshSystemAppearance();
        return TRUE;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        layoutDialogControls(controlLayouts_, GetDpiForWindow(window_));
        refreshSystemAppearance();
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == idHotkeyKey && HIWORD(wParam) == CBN_DROPDOWN) {
            SendMessageW(hotkeyKeyCombo_, CB_SETTOPINDEX, 0, 0);
            PostMessageW(hotkeyKeyCombo_, CB_SETTOPINDEX, 0, 0);
            return TRUE;
        }
        if ((LOWORD(wParam) == idHotkeyEnabled || LOWORD(wParam) == idScreenEdgeEnabled)
            && HIWORD(wParam) == BN_CLICKED) {
            updateActivationEnabledState();
            return TRUE;
        }
        if (LOWORD(wParam) == idOpacityEdit && HIWORD(wParam) == EN_CHANGE) {
            const auto opacity = readUnsigned(opacityEdit_);
            if (opacity && *opacity >= 30U && *opacity <= 100U) {
                SendMessageW(opacitySlider_, TBM_SETPOS, TRUE, static_cast<LPARAM>(*opacity));
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED) {
            if (applyAllFromControls()) {
                hide();
            }
            return TRUE;
        }
        if (LOWORD(wParam) == idApplyAll && HIWORD(wParam) == BN_CLICKED) {
            static_cast<void>(applyAllFromControls());
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            syncAppearanceControls();
            syncActivationControls();
            syncDiagnosticsControls();
            hide();
            return TRUE;
        }
        return FALSE;
    case WM_NOTIFY:
        if (reinterpret_cast<const NMHDR*>(lParam)->hwndFrom == tabControl_
            && reinterpret_cast<const NMHDR*>(lParam)->code == TCN_SELCHANGE) {
            updateVisiblePage();
            return TRUE;
        }
        return FALSE;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == opacitySlider_) {
            const auto opacity = SendMessageW(opacitySlider_, TBM_GETPOS, 0, 0);
            SetWindowTextW(opacityEdit_, std::to_wstring(opacity).c_str());
            return TRUE;
        }
        return FALSE;
    case WM_ERASEBKGND: {
        RECT bounds{};
        GetClientRect(window_, &bounds);
        FillRect(reinterpret_cast<HDC>(wParam), &bounds, GetSysColorBrush(COLOR_BTNFACE));
        return TRUE;
    }
    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        const auto dc = reinterpret_cast<HDC>(wParam);
        const auto control = reinterpret_cast<HWND>(lParam);
        if (control == tabPageBackground_) {
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        const bool isPageControl =
            std::ranges::find(generalPageControls_, control) != generalPageControls_.end()
            || std::ranges::find(activationPageControls_, control)
                   != activationPageControls_.end();
        SetTextColor(dc, IsWindowEnabled(control) ? GetSysColor(COLOR_WINDOWTEXT)
                                                   : GetSysColor(COLOR_GRAYTEXT));
        const bool isPageOverlay =
            std::ranges::find(pageOverlayControls_, control) != pageOverlayControls_.end();
        if (isPageOverlay) {
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetStockObject(HOLLOW_BRUSH));
        }
        if (isPageControl) {
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        SetBkMode(dc, TRANSPARENT);
        SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CLOSE:
        hide();
        return TRUE;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window_, DWLP_USER, 0);
        window_ = nullptr;
        controlLayouts_.clear();
        generalPageControls_.clear();
        activationPageControls_.clear();
        pageOverlayControls_.clear();
        return TRUE;
    default:
        if (isSystemAppearanceMessage(message)) {
            refreshSystemAppearance();
            return TRUE;
        }
        return FALSE;
    }
}

bool SettingsWindow::create(const HINSTANCE instance, const HWND owner)
{
    INITCOMMONCONTROLSEX commonControls{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES,
    };
    static_cast<void>(InitCommonControlsEx(&commonControls));
    const NativeDialogTemplate dialogTemplate{};
    window_ = CreateDialogIndirectParamW(instance, dialogTemplate.get(), owner, dialogProcedure,
                                         reinterpret_cast<LPARAM>(this));
    return window_ != nullptr;
}

void SettingsWindow::createControls()
{
    const UINT dpi = GetDpiForWindow(window_);
    static_cast<void>(systemUiFont_.refresh(dpi));
    const auto font = systemUiFont_.get();
    auto add = [&](const wchar_t* cls, const wchar_t* text, const DWORD style, const int x,
                   const int y, const int width, const int height, const int id) {
        const auto control = CreateWindowExW(
            0, cls, text, WS_CHILD | WS_VISIBLE | style, scaleDip(x, dpi), scaleDip(y, dpi),
            scaleDip(width, dpi), scaleDip(height, dpi), window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        controlLayouts_.push_back({control, x, y, width, height});
        return control;
    };
    auto addPage = [&](std::vector<HWND>& page, const wchar_t* cls, const wchar_t* text,
                       const DWORD style, const int x, const int y, const int width,
                       const int height, const int id) {
        const auto control = add(cls, text, style, x, y, width, height, id);
        const std::wstring_view className{cls};
        if (className == L"STATIC" || className == L"BUTTON") {
            const auto extendedStyle = GetWindowLongPtrW(control, GWL_EXSTYLE);
            SetWindowLongPtrW(control, GWL_EXSTYLE, extendedStyle | WS_EX_TRANSPARENT);
            pageOverlayControls_.push_back(control);
        }
        page.push_back(control);
        return control;
    };
    const auto compactCombo = [dpi](const HWND combo) {
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                     scaleDip(compactComboSelectionHeightDip, dpi));
    };

    tabControl_ = add(
        WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, 12, 12, 536, 520, idTab);
    TCITEMW tabItem{.mask = TCIF_TEXT};
    tabItem.pszText = const_cast<wchar_t*>(L"常规");
    SendMessageW(tabControl_, TCM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&tabItem));
    tabItem.pszText = const_cast<wchar_t*>(L"唤起");
    SendMessageW(tabControl_, TCM_INSERTITEMW, 1, reinterpret_cast<LPARAM>(&tabItem));
    SendMessageW(tabControl_, TCM_SETCURSEL, 0, 0);

    RECT tabPageBounds{};
    GetClientRect(tabControl_, &tabPageBounds);
    TabCtrl_AdjustRect(tabControl_, FALSE, &tabPageBounds);
    MapWindowPoints(tabControl_, window_, reinterpret_cast<POINT*>(&tabPageBounds), 2);
    const auto toDip = [dpi](const LONG pixels) {
        return MulDiv(pixels, 96, static_cast<int>(dpi));
    };
    tabPageBackground_ = add(
        L"STATIC", L"", SS_LEFT,
        toDip(tabPageBounds.left), toDip(tabPageBounds.top),
        toDip(tabPageBounds.right - tabPageBounds.left),
        toDip(tabPageBounds.bottom - tabPageBounds.top), idTabPageBackground);

    addPage(generalPageControls_, L"BUTTON", L"主窗口", BS_GROUPBOX,
            28, 50, 504, 150, 0);
    addPage(generalPageControls_, L"STATIC", L"背景效果：", 0,
            48, 78, 70, 20, 0);
    backdropCombo_ = addPage(
        generalPageControls_, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 118, 72, 220, 180, idBackdrop);
    compactCombo(backdropCombo_);
    const auto addBackdrop = [this](const wchar_t* label, const core::BackdropMode mode) {
        const auto index = SendMessageW(
            backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (index != CB_ERR && index != CB_ERRSPACE) {
            SendMessageW(backdropCombo_, CB_SETITEMDATA, index, static_cast<LPARAM>(mode));
        }
    };
    addBackdrop(L"纯色（Solid）", core::BackdropMode::Solid);
    addBackdrop(L"云母（Mica）", core::BackdropMode::Mica);
    addBackdrop(L"亚克力磨砂（Acrylic）", core::BackdropMode::Acrylic);
    addBackdrop(L"标签式云母（Tabbed）", core::BackdropMode::Tabbed);
    addPage(generalPageControls_, L"STATIC", L"透明度：", 0,
            48, 118, 70, 20, 0);
    opacitySlider_ = addPage(
        generalPageControls_, TRACKBAR_CLASSW, L"",
        TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP, 118, 108, 264, 36, idOpacitySlider);
    SendMessageW(opacitySlider_, TBM_SETRANGE, TRUE, MAKELPARAM(30, 100));
    SendMessageW(opacitySlider_, TBM_SETTICFREQ, 10, 0);
    SendMessageW(opacitySlider_, TBM_SETPAGESIZE, 0, 5);
    opacityEdit_ = addPage(
        generalPageControls_, L"EDIT", L"95",
        WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_RIGHT, 396, 114, 56,
        compactEditHeightDip, idOpacityEdit);
    SendMessageW(opacityEdit_, EM_SETLIMITTEXT, 3, 0);
    addPage(generalPageControls_, L"STATIC", L"%", 0, 460, 118, 24, 20, 0);
    addPage(generalPageControls_, L"STATIC",
            L"背景效果与整体透明度同时作用于 Launcher 和搜索窗；不支持的效果会安全降级。",
            0, 48, 152, 450, 34, 0);

    addPage(generalPageControls_, L"BUTTON", L"诊断", BS_GROUPBOX,
            28, 212, 504, 112, 0);
    diagnosticLoggingEnabledCheck_ = addPage(
        generalPageControls_, L"BUTTON", L"启用诊断日志",
        BS_AUTOCHECKBOX | WS_TABSTOP, 48, 238, 220, 24, idDiagnosticLoggingEnabled);
    diagnosticsStatusText_ = addPage(
        generalPageControls_, L"STATIC",
        L"日志用于定位启动、快捷键和边缘唤起问题，不记录条目名称、目标、参数或搜索词。",
        0, 48, 270, 450, 40, 0);

    addPage(activationPageControls_, L"BUTTON", L"全局快捷键", BS_GROUPBOX,
            28, 50, 504, 98, 0);
    hotkeyEnabledCheck_ = addPage(
        activationPageControls_, L"BUTTON", L"启用全局快捷键",
        BS_AUTOCHECKBOX | WS_TABSTOP, 48, 76, 170, 24, idHotkeyEnabled);
    addPage(activationPageControls_, L"STATIC", L"组合键：", 0,
            48, 112, 70, 20, 0);
    altCheck_ = addPage(
        activationPageControls_, L"BUTTON", L"Alt",
        BS_AUTOCHECKBOX | WS_TABSTOP, 118, 106, 54, 26, idHotkeyAlt);
    controlCheck_ = addPage(
        activationPageControls_, L"BUTTON", L"Ctrl",
        BS_AUTOCHECKBOX | WS_TABSTOP, 176, 106, 58, 26, idHotkeyControl);
    shiftCheck_ = addPage(
        activationPageControls_, L"BUTTON", L"Shift",
        BS_AUTOCHECKBOX | WS_TABSTOP, 238, 106, 62, 26, idHotkeyShift);
    winCheck_ = addPage(
        activationPageControls_, L"BUTTON", L"Win",
        BS_AUTOCHECKBOX | WS_TABSTOP, 304, 106, 58, 26, idHotkeyWin);
    hotkeyKeyCombo_ = addPage(
        activationPageControls_, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP,
        378, 106, 112, 260, idHotkeyKey);
    compactCombo(hotkeyKeyCombo_);
    SendMessageW(hotkeyKeyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Space"));
    for (wchar_t key = L'0'; key <= L'9'; ++key) {
        const wchar_t value[]{key, L'\0'};
        SendMessageW(hotkeyKeyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    for (wchar_t key = L'A'; key <= L'Z'; ++key) {
        const wchar_t value[]{key, L'\0'};
        SendMessageW(hotkeyKeyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    for (int number = 1; number <= 24; ++number) {
        if (number == 12) continue;
        const auto value = L"F" + std::to_wstring(number);
        SendMessageW(hotkeyKeyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
    }

    addPage(activationPageControls_, L"BUTTON", L"屏幕边缘", BS_GROUPBOX,
            28, 160, 504, 342, 0);
    screenEdgeEnabledCheck_ = addPage(
        activationPageControls_, L"BUTTON", L"启用屏幕边缘停留唤起",
        BS_AUTOCHECKBOX | WS_TABSTOP, 48, 186, 240, 24, idScreenEdgeEnabled);
    addPage(activationPageControls_, L"STATIC", L"热区：", 0,
            48, 222, 54, 20, 0);
    leftZoneCheck_ = addPage(activationPageControls_, L"BUTTON", L"左",
        BS_AUTOCHECKBOX | WS_TABSTOP, 102, 216, 44, 26, idEdgeLeft);
    rightZoneCheck_ = addPage(activationPageControls_, L"BUTTON", L"右",
        BS_AUTOCHECKBOX | WS_TABSTOP, 148, 216, 44, 26, idEdgeRight);
    topZoneCheck_ = addPage(activationPageControls_, L"BUTTON", L"上",
        BS_AUTOCHECKBOX | WS_TABSTOP, 194, 216, 44, 26, idEdgeTop);
    bottomZoneCheck_ = addPage(activationPageControls_, L"BUTTON", L"下",
        BS_AUTOCHECKBOX | WS_TABSTOP, 240, 216, 44, 26, idEdgeBottom);
    topLeftZoneCheck_ = addPage(activationPageControls_, L"BUTTON", L"左上",
        BS_AUTOCHECKBOX | WS_TABSTOP, 286, 216, 56, 26, idEdgeTopLeft);
    topRightZoneCheck_ = addPage(activationPageControls_, L"BUTTON", L"右上",
        BS_AUTOCHECKBOX | WS_TABSTOP, 344, 216, 56, 26, idEdgeTopRight);
    bottomLeftZoneCheck_ = addPage(activationPageControls_, L"BUTTON", L"左下",
        BS_AUTOCHECKBOX | WS_TABSTOP, 402, 216, 56, 26, idEdgeBottomLeft);
    bottomRightZoneCheck_ = addPage(activationPageControls_, L"BUTTON", L"右下",
        BS_AUTOCHECKBOX | WS_TABSTOP, 460, 216, 56, 26, idEdgeBottomRight);

    addPage(activationPageControls_, L"STATIC", L"多显示器：", 0,
            48, 256, 82, 20, 0);
    edgeModeCombo_ = addPage(
        activationPageControls_, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 130, 252, 220, 160, idEdgeMode);
    compactCombo(edgeModeCombo_);
    SendMessageW(edgeModeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"仅桌面外轮廓"));
    SendMessageW(edgeModeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"每台显示器边缘"));

    fullscreenCheck_ = addPage(
        activationPageControls_, L"BUTTON", L"全屏应用运行时禁用边缘唤起",
        BS_AUTOCHECKBOX | WS_TABSTOP, 48, 290, 280, 24, idFullscreenSuppression);
    addPage(activationPageControls_, L"STATIC", L"进程黑名单：", 0,
            48, 328, 94, 20, 0);
    processBlocklistEdit_ = addPage(
        activationPageControls_, L"EDIT", L"",
        WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        142, 322, 356, 58, idProcessBlocklist);
    addPage(activationPageControls_, L"STATIC", L"进程白名单：", 0,
            48, 394, 94, 20, 0);
    processAllowlistEdit_ = addPage(
        activationPageControls_, L"EDIT", L"",
        WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        142, 388, 356, 58, idProcessAllowlist);
    SendMessageW(processBlocklistEdit_, EM_SETLIMITTEXT, 4096, 0);
    SendMessageW(processAllowlistEdit_, EM_SETLIMITTEXT, 4096, 0);
    activationStatusText_ = addPage(
        activationPageControls_, L"STATIC",
        L"每行填写一个进程名；也兼容逗号或分号。白名单非空时优先于黑名单。",
        0, 48, 458, 450, 34, 0);

    settingsStatusText_ = add(
        L"STATIC", L"修改后选择“应用”或“确定”保存设置。",
        0, 18, 552, 244, 24, 0);
    add(L"BUTTON", L"确定", WS_TABSTOP | BS_DEFPUSHBUTTON,
        290, 544, 78, 30, IDOK);
    add(L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON,
        378, 544, 78, 30, IDCANCEL);
    add(L"BUTTON", L"应用", WS_TABSTOP | BS_PUSHBUTTON,
        466, 544, 78, 30, idApplyAll);

    syncAppearanceControls();
    syncActivationControls();
    syncDiagnosticsControls();
    updateVisiblePage();
}

bool SettingsWindow::applyAllFromControls()
{
    if (!applyAppearanceFromControls()
        || !applyActivationFromControls()
        || !applyDiagnosticsFromControls()) {
        return false;
    }
    SetWindowTextW(settingsStatusText_, L"设置已应用，正在后台保存配置。");
    return true;
}

bool SettingsWindow::applyAppearanceFromControls()
{
    const auto backdropSelection = SendMessageW(backdropCombo_, CB_GETCURSEL, 0, 0);
    if (backdropSelection == CB_ERR) {
        SendMessageW(tabControl_, TCM_SETCURSEL, 0, 0);
        updateVisiblePage();
        SetWindowTextW(settingsStatusText_, L"请选择主窗口背景效果。");
        MessageBeep(MB_ICONWARNING);
        SetFocus(backdropCombo_);
        return false;
    }
    const auto opacity = readUnsigned(opacityEdit_);
    if (!opacity || *opacity < 30U || *opacity > 100U) {
        SendMessageW(tabControl_, TCM_SETCURSEL, 0, 0);
        updateVisiblePage();
        SetWindowTextW(settingsStatusText_, L"透明度必须是 30 到 100 之间的整数。");
        MessageBeep(MB_ICONWARNING);
        SetFocus(opacityEdit_);
        SendMessageW(opacityEdit_, EM_SETSEL, 0, -1);
        return false;
    }
    auto requested = appearance_;
    requested.backdrop = static_cast<core::BackdropMode>(
        SendMessageW(backdropCombo_, CB_GETITEMDATA, backdropSelection, 0));
    requested.opacityPercent = static_cast<std::uint8_t>(*opacity);
    if (requested == appearance_) {
        return true;
    }
    if (appearanceChangedHandler_ && !appearanceChangedHandler_(requested)) {
        syncAppearanceControls();
        SetWindowTextW(settingsStatusText_, L"无法保存主窗口外观，已恢复原值。");
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    appearance_ = requested;
    syncAppearanceControls();
    return true;
}

bool SettingsWindow::applyActivationFromControls()
{
    auto requested = activation_;
    requested.hotkey.enabled = IsDlgButtonChecked(window_, idHotkeyEnabled) == BST_CHECKED;
    requested.hotkey.modifiers.clear();
    if (IsDlgButtonChecked(window_, idHotkeyAlt) == BST_CHECKED)
        requested.hotkey.modifiers.push_back(core::HotkeyModifier::Alt);
    if (IsDlgButtonChecked(window_, idHotkeyControl) == BST_CHECKED)
        requested.hotkey.modifiers.push_back(core::HotkeyModifier::Control);
    if (IsDlgButtonChecked(window_, idHotkeyShift) == BST_CHECKED)
        requested.hotkey.modifiers.push_back(core::HotkeyModifier::Shift);
    if (IsDlgButtonChecked(window_, idHotkeyWin) == BST_CHECKED)
        requested.hotkey.modifiers.push_back(core::HotkeyModifier::Win);
    wchar_t key[32]{};
    const auto selected = SendMessageW(hotkeyKeyCombo_, CB_GETCURSEL, 0, 0);
    if (selected != CB_ERR)
        SendMessageW(hotkeyKeyCombo_, CB_GETLBTEXT, selected, reinterpret_cast<LPARAM>(key));
    requested.hotkey.key = narrowAscii(key);
    requested.screenEdge.enabled = IsDlgButtonChecked(window_, idScreenEdgeEnabled) == BST_CHECKED;
    requested.screenEdge.disableOnFullscreen =
        IsDlgButtonChecked(window_, idFullscreenSuppression) == BST_CHECKED;
    requested.screenEdge.zones.clear();
    const auto addZone = [&](const int id, const core::ScreenEdgeZone zone) {
        if (IsDlgButtonChecked(window_, id) == BST_CHECKED) requested.screenEdge.zones.push_back(zone);
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
    requested.screenEdge.thicknessDip = core::defaultScreenEdgeThicknessDip;
    requested.screenEdge.cornerSizeDip = core::defaultScreenEdgeCornerSizeDip;
    requested.screenEdge.dwellMs = core::defaultScreenEdgeDwellMs;
    requested.screenEdge.pollMs = core::defaultScreenEdgePollMs;
    requested.screenEdge.cooldownMs = core::defaultScreenEdgeCooldownMs;
    const auto rejectField = [this](const HWND field, const wchar_t* message) {
        SendMessageW(tabControl_, TCM_SETCURSEL, 1, 0);
        updateVisiblePage();
        SetWindowTextW(activationStatusText_, message);
        SetWindowTextW(settingsStatusText_, L"请修正“唤起”页中的无效设置。");
        MessageBeep(MB_ICONWARNING);
        SetFocus(field);
        SendMessageW(field, EM_SETSEL, 0, -1);
        return false;
    };
    const auto blocklist = parseProcessList(processBlocklistEdit_);
    if (!blocklist) {
        return rejectField(processBlocklistEdit_, blocklist.error().c_str());
    }
    const auto allowlist = parseProcessList(processAllowlistEdit_);
    if (!allowlist) {
        return rejectField(processAllowlistEdit_, allowlist.error().c_str());
    }
    requested.screenEdge.foregroundProcessBlocklist = *blocklist;
    requested.screenEdge.foregroundProcessAllowlist = *allowlist;
    if (requested.hotkey.modifiers.empty()) {
        SendMessageW(tabControl_, TCM_SETCURSEL, 1, 0);
        updateVisiblePage();
        SetWindowTextW(activationStatusText_, L"快捷键至少需要一个修饰键。");
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    if (requested.screenEdge.zones.empty()) {
        SendMessageW(tabControl_, TCM_SETCURSEL, 1, 0);
        updateVisiblePage();
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
    SetWindowTextW(activationStatusText_, L"激活设置已生效，正在后台保存。");
    return true;
}

bool SettingsWindow::applyDiagnosticsFromControls()
{
    const bool requested =
        IsDlgButtonChecked(window_, idDiagnosticLoggingEnabled) == BST_CHECKED;
    if (requested == diagnosticLoggingEnabled_) {
        SetWindowTextW(diagnosticsStatusText_,
                       requested ? L"诊断日志已启用。" : L"诊断日志已关闭。");
        return true;
    }
    if (diagnosticsChangedHandler_) {
        const auto result = diagnosticsChangedHandler_(requested);
        if (!result) {
            syncDiagnosticsControls();
            SetWindowTextW(diagnosticsStatusText_, result.error().c_str());
            MessageBeep(MB_ICONWARNING);
            return false;
        }
    }
    diagnosticLoggingEnabled_ = requested;
    syncDiagnosticsControls();
    SetWindowTextW(diagnosticsStatusText_, L"日志设置已生效，正在后台保存。");
    return true;
}

void SettingsWindow::syncAppearanceControls()
{
    if (!backdropCombo_ || !opacitySlider_ || !opacityEdit_) {
        return;
    }
    const auto count = SendMessageW(backdropCombo_, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        if (static_cast<core::BackdropMode>(
                SendMessageW(backdropCombo_, CB_GETITEMDATA, index, 0))
            == appearance_.backdrop) {
            SendMessageW(backdropCombo_, CB_SETCURSEL, index, 0);
            break;
        }
    }
    SendMessageW(opacitySlider_, TBM_SETPOS, TRUE, appearance_.opacityPercent);
    SetWindowTextW(
        opacityEdit_, std::to_wstring(appearance_.opacityPercent).c_str());
}

void SettingsWindow::syncActivationControls()
{
    if (!hotkeyEnabledCheck_) return;
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
    auto selected = SendMessageW(hotkeyKeyCombo_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                 reinterpret_cast<LPARAM>(key.c_str()));
    if (selected == CB_ERR) selected = 0;
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
    CheckDlgButton(window_, idFullscreenSuppression,
                   activation_.screenEdge.disableOnFullscreen ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(
        processBlocklistEdit_,
        formatProcessList(activation_.screenEdge.foregroundProcessBlocklist).c_str());
    SetWindowTextW(
        processAllowlistEdit_,
        formatProcessList(activation_.screenEdge.foregroundProcessAllowlist).c_str());
    updateActivationEnabledState();
}

void SettingsWindow::syncDiagnosticsControls()
{
    if (!diagnosticLoggingEnabledCheck_) {
        return;
    }
    CheckDlgButton(
        window_,
        idDiagnosticLoggingEnabled,
        diagnosticLoggingEnabled_ ? BST_CHECKED : BST_UNCHECKED);
}

void SettingsWindow::updateActivationEnabledState()
{
    const BOOL hotkeyEnabled = IsDlgButtonChecked(window_, idHotkeyEnabled) == BST_CHECKED;
    for (const auto control : {altCheck_, controlCheck_, shiftCheck_, winCheck_, hotkeyKeyCombo_})
        EnableWindow(control, hotkeyEnabled);
    const BOOL edgeEnabled = IsDlgButtonChecked(window_, idScreenEdgeEnabled) == BST_CHECKED;
    for (const auto control : {leftZoneCheck_, rightZoneCheck_, topZoneCheck_, bottomZoneCheck_,
                               topLeftZoneCheck_, topRightZoneCheck_, bottomLeftZoneCheck_,
                               bottomRightZoneCheck_, edgeModeCombo_, fullscreenCheck_,
                               processBlocklistEdit_, processAllowlistEdit_})
        EnableWindow(control, edgeEnabled);
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void SettingsWindow::updateVisiblePage()
{
    if (!tabControl_) {
        return;
    }
    const bool showActivation = SendMessageW(tabControl_, TCM_GETCURSEL, 0, 0) == 1;
    for (const auto control : generalPageControls_) {
        ShowWindow(control, showActivation ? SW_HIDE : SW_SHOWNA);
    }
    for (const auto control : activationPageControls_) {
        ShowWindow(control, showActivation ? SW_SHOWNA : SW_HIDE);
    }
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
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
    const int maximumX = std::max(monitorInfo.rcWork.left, monitorInfo.rcWork.right - width);
    const int maximumY = std::max(monitorInfo.rcWork.top, monitorInfo.rcWork.bottom - height);
    SetWindowPos(window_, nullptr,
                 std::clamp(desiredX, static_cast<int>(monitorInfo.rcWork.left), maximumX),
                 std::clamp(desiredY, static_cast<int>(monitorInfo.rcWork.top), maximumY), 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

} // namespace hlaunch::ui
