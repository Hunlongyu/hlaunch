#include "ui/settings_window.h"

#include "ui/theme.h"

#include <windowsx.h>

#include <algorithm>
#include <array>

namespace hlaunch::ui {
namespace {

constexpr wchar_t settingsWindowClass[] = L"HLaunch.SettingsWindow.v1";
constexpr int settingsWidth = 460;
constexpr int settingsHeight = 280;
constexpr int headerHeight = 48;
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
    themeChangedHandler_ = std::move(themeChangedHandler);
    if (!window_ && !create(instance, owner)) {
        return false;
    }
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    setThemeMode(themeMode);
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
    rebuildThemeResources();
    if (themeCombo_) {
        SendMessageW(themeCombo_, CB_SETCURSEL, themeMode_ == core::ThemeMode::Light ? 1 : 0, 0);
        applyNativeControlTheme(themeCombo_, themeMode_);
    }
    applyNativeWindowTheme(window_, themeMode_);
    InvalidateRect(window_, nullptr, TRUE);
}

HWND SettingsWindow::handle() const noexcept
{
    return window_;
}

bool SettingsWindow::isVisible() const noexcept
{
    return window_ && IsWindowVisible(window_);
}

LRESULT CALLBACK SettingsWindow::windowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    SettingsWindow* self{};
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam); // NOLINT(performance-no-int-to-ptr): Win32 LPARAM carries CREATESTRUCTW*.
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr): Win32 stores this pointer as LONG_PTR.
    }
    return self ? self->handleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SettingsWindow::handleMessage(
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        createControls();
        applyNativeWindowTheme(window_, themeMode_);
        return 0;
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == idTheme && HIWORD(wParam) == CBN_SELCHANGE) {
            const auto selected = SendMessageW(themeCombo_, CB_GETCURSEL, 0, 0);
            const auto requested = selected == 1 ? core::ThemeMode::Light : core::ThemeMode::Dark;
            if (!themeChangedHandler_ || themeChangedHandler_(requested)) {
                setThemeMode(requested);
            }
            else {
                SendMessageW(themeCombo_, CB_SETCURSEL, themeMode_ == core::ThemeMode::Light ? 1 : 0, 0);
            }
            return 0;
        }
        if (LOWORD(wParam) == idClose) {
            hide();
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        const auto context = reinterpret_cast<HDC>(wParam); // NOLINT(performance-no-int-to-ptr): Win32 passes HDC in WPARAM.
        SetTextColor(context, toColorRef(palette_.text));
        const bool surface = message != WM_CTLCOLORSTATIC;
        SetBkColor(context, toColorRef(surface ? palette_.surface : palette_.background));
        return reinterpret_cast<LRESULT>(surface ? surfaceBrush_.get() : backgroundBrush_.get()); // NOLINT(performance-no-int-to-ptr): Win32 expects HBRUSH in LRESULT.
    }
    case WM_DRAWITEM:
    {
        const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam); // NOLINT(performance-no-int-to-ptr): Win32 LPARAM carries DRAWITEMSTRUCT*.
        if (item.CtlType == ODT_COMBOBOX) {
            drawComboItem(item);
        }
        else {
            drawButton(item);
        }
        return TRUE;
    }
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window_, &point);
        return point.y >= 0 && point.y < headerHeight && !closeHit(point) ? HTCAPTION : HTCLIENT;
    }
    case WM_LBUTTONUP: {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (closeHit(point)) {
            hide();
        }
        return 0;
    }
    case WM_CLOSE:
        hide();
        return 0;
    case WM_DESTROY:
        window_ = nullptr;
        themeCombo_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

bool SettingsWindow::create(const HINSTANCE instance, const HWND owner)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    windowClass.lpfnWndProc = &SettingsWindow::windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = settingsWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    rebuildThemeResources();
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        settingsWindowClass,
        L"HLaunch 设置",
        WS_POPUP,
        0,
        0,
        settingsWidth,
        settingsHeight,
        owner,
        nullptr,
        instance,
        this);
    return window_ != nullptr;
}

void SettingsWindow::createControls()
{
    const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto add = [&](const wchar_t* cls, const wchar_t* text, const DWORD style,
                   const int x, const int y, const int width, const int height, const int id) {
        const auto control = CreateWindowExW(
            0,
            cls,
            text,
            WS_CHILD | WS_VISIBLE | style,
            x,
            y,
            width,
            height,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), // NOLINT(performance-no-int-to-ptr): Child controls encode their integer ID in HMENU.
            GetModuleHandleW(nullptr),
            nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        applyNativeControlTheme(control, themeMode_);
        return control;
    };

    add(L"STATIC", L"外观", 0, 24, 66, 100, 24, 0);
    add(L"STATIC", L"主题", 0, 24, 112, 100, 24, 0);
    themeCombo_ = add(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
        144,
        106,
        276,
        180,
        idTheme);
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"深色"));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"浅色"));
    SendMessageW(themeCombo_, CB_SETCURSEL, themeMode_ == core::ThemeMode::Light ? 1 : 0, 0);
    add(
        L"STATIC",
        L"切换后立即应用到主窗口、搜索和编辑界面，并保存到 config.json。",
        0,
        144,
        144,
        276,
        42,
        0);
    add(L"BUTTON", L"关闭", BS_OWNERDRAW | WS_TABSTOP, 325, 216, 95, 34, idClose);
}

void SettingsWindow::rebuildThemeResources()
{
    palette_ = paletteFor(themeMode_);
    backgroundBrush_.reset(CreateSolidBrush(toColorRef(palette_.background)));
    surfaceBrush_.reset(CreateSolidBrush(toColorRef(palette_.surface)));
}

void SettingsWindow::paint()
{
    PAINTSTRUCT paintState{};
    const auto context = BeginPaint(window_, &paintState);
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(context, &client, backgroundBrush_.get());
    const auto oldFont = SelectObject(context, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(context, TRANSPARENT);
    SetTextColor(context, toColorRef(palette_.text));
    RECT titleBounds{52, 0, settingsWidth - 56, headerHeight};
    DrawTextW(context, L"设置", -1, &titleBounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const auto accent = CreateSolidBrush(toColorRef(palette_.accent));
    RECT logo{20, 15, 38, 33};
    FillRect(context, &logo, accent);
    DeleteObject(accent);
    const auto pen = CreatePen(PS_SOLID, 2, toColorRef(palette_.textMuted));
    const auto oldPen = SelectObject(context, pen);
    MoveToEx(context, settingsWidth - 30, 18, nullptr);
    LineTo(context, settingsWidth - 20, 28);
    MoveToEx(context, settingsWidth - 20, 18, nullptr);
    LineTo(context, settingsWidth - 30, 28);
    SelectObject(context, oldPen);
    DeleteObject(pen);
    SelectObject(context, oldFont);
    EndPaint(window_, &paintState);
}

void SettingsWindow::drawButton(const DRAWITEMSTRUCT& item) const
{
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const auto fill = CreateSolidBrush(toColorRef(pressed ? palette_.surface : palette_.elevated));
    const auto border = CreatePen(PS_SOLID, 1, toColorRef(palette_.border));
    const auto oldBrush = SelectObject(item.hDC, fill);
    const auto oldPen = SelectObject(item.hDC, border);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, 10, 10);
    SelectObject(item.hDC, oldBrush);
    SelectObject(item.hDC, oldPen);
    DeleteObject(fill);
    DeleteObject(border);

    wchar_t text[32]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, toColorRef(palette_.text));
    RECT bounds = item.rcItem;
    DrawTextW(item.hDC, text, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((item.itemState & ODS_FOCUS) != 0) {
        InflateRect(&bounds, -4, -4);
        DrawFocusRect(item.hDC, &bounds);
    }
}

void SettingsWindow::drawComboItem(const DRAWITEMSTRUCT& item) const
{
    const bool dropdownSelection = (item.itemState & ODS_SELECTED) != 0
        && (item.itemState & ODS_COMBOBOXEDIT) == 0;
    const auto background = dropdownSelection ? palette_.accent : palette_.surface;
    const auto brush = CreateSolidBrush(toColorRef(background));
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);

    const auto selectedIndex = item.itemID == static_cast<UINT>(-1)
        ? static_cast<UINT>(SendMessageW(item.hwndItem, CB_GETCURSEL, 0, 0))
        : item.itemID;
    wchar_t text[64]{};
    if (selectedIndex != static_cast<UINT>(CB_ERR)) {
        SendMessageW(item.hwndItem, CB_GETLBTEXT, selectedIndex, reinterpret_cast<LPARAM>(text));
    }
    SetBkMode(item.hDC, TRANSPARENT);
    const auto textColor = dropdownSelection
        ? (themeMode_ == core::ThemeMode::Dark ? palette_.background : 0xFFFFFFU)
        : palette_.text;
    SetTextColor(item.hDC, toColorRef(textColor));
    RECT bounds = item.rcItem;
    bounds.left += 6;
    DrawTextW(item.hDC, text, -1, &bounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if ((item.itemState & ODS_FOCUS) != 0) {
        bounds = item.rcItem;
        InflateRect(&bounds, -2, -2);
        DrawFocusRect(item.hDC, &bounds);
    }
}

bool SettingsWindow::closeHit(const POINT point) const noexcept
{
    return point.x >= settingsWidth - 48 && point.x < settingsWidth
        && point.y >= 0 && point.y < headerHeight;
}

void SettingsWindow::positionOverOwner(const HWND owner)
{
    RECT ownerBounds{};
    if (!owner || !GetWindowRect(owner, &ownerBounds)) {
        return;
    }
    const int x = ownerBounds.left + ((ownerBounds.right - ownerBounds.left - settingsWidth) / 2);
    const int y = ownerBounds.top + ((ownerBounds.bottom - ownerBounds.top - settingsHeight) / 2);
    SetWindowPos(
        window_,
        HWND_TOP,
        x,
        y,
        settingsWidth,
        settingsHeight,
        SWP_NOACTIVATE);
}

} // namespace hlaunch::ui
