#include "platform/windows/tray_icon.h"

#include <algorithm>
#include <iterator>

namespace hlaunch::platform::windows {

wil::unique_hmenu createTrayContextMenu(
    const bool launcherVisible,
    const std::optional<bool> startupEnabled)
{
    wil::unique_hmenu menu{CreatePopupMenu()};
    if (!menu) {
        return {};
    }

    AppendMenuW(
        menu.get(),
        MF_STRING,
        trayToggleMenuId,
        launcherVisible ? L"隐藏 HLaunch" : L"显示 HLaunch");
    UINT startupFlags = MF_STRING;
    if (!startupEnabled.has_value()) {
        startupFlags |= MF_GRAYED;
    } else if (*startupEnabled) {
        startupFlags |= MF_CHECKED;
    }
    AppendMenuW(menu.get(), startupFlags, trayStartupMenuId, L"开机自启");
    AppendMenuW(menu.get(), MF_STRING, traySettingsMenuId, L"设置");
    AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu.get(), MF_STRING, trayExitMenuId, L"退出");
    return menu;
}

TrayIcon::~TrayIcon()
{
    stop();
}

bool TrayIcon::start(const HWND owner, const HICON icon)
{
    stop();
    if (!IsWindow(owner)) {
        return false;
    }
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreatedMessage_ == 0) {
        return false;
    }

    data_ = {};
    data_.cbSize = sizeof(NOTIFYICONDATAW);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = trayIconCallbackMessage;
    data_.hIcon = icon;
    constexpr auto tooltip = trayIconTooltipText();
    static_assert(
        tooltip.size() < sizeof(NOTIFYICONDATAW::szTip) / sizeof(wchar_t));
    std::ranges::copy(tooltip, data_.szTip);
    data_.szTip[tooltip.size()] = L'\0';
    return addOrRetry();
}

void TrayIcon::stop() noexcept
{
    if (data_.hWnd) {
        KillTimer(data_.hWnd, trayIconRetryTimerId);
    }
    if (added_) {
        notifyIcon_(NIM_DELETE, &data_);
    }
    data_ = {};
    taskbarCreatedMessage_ = 0;
    added_ = false;
}

std::optional<TrayCommand> TrayIcon::handleMessage(
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam,
    const bool launcherVisible,
    const std::optional<bool> startupEnabled)
{
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        added_ = false;
        static_cast<void>(addOrRetry());
        return std::nullopt;
    }
    if (message == WM_TIMER && wParam == trayIconRetryTimerId && data_.hWnd) {
        if (!added_) {
            static_cast<void>(addOrRetry());
        }
        return std::nullopt;
    }
    if (message != trayIconCallbackMessage || !added_) {
        return std::nullopt;
    }

    switch (LOWORD(lParam)) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
    case WM_LBUTTONDBLCLK:
        return TrayCommand::ToggleLauncher;
    case WM_CONTEXTMENU:
    case WM_RBUTTONUP:
        return showContextMenu(launcherVisible, startupEnabled);
    default:
        return std::nullopt;
    }
}

bool TrayIcon::isAdded() const noexcept
{
    return added_;
}

UINT TrayIcon::taskbarCreatedMessage() const noexcept
{
    return taskbarCreatedMessage_;
}

bool TrayIcon::add()
{
    if (!notifyIcon_(NIM_ADD, &data_)) {
        added_ = false;
        return false;
    }
    data_.uVersion = NOTIFYICON_VERSION_4;
    notifyIcon_(NIM_SETVERSION, &data_);
    added_ = true;
    return true;
}

bool TrayIcon::addOrRetry()
{
    if (add()) {
        KillTimer(data_.hWnd, trayIconRetryTimerId);
        return true;
    }
    // Only the notification icon waits for Explorer; activation stays usable.
    return SetTimer(data_.hWnd, trayIconRetryTimerId, 500, nullptr) != 0;
}

std::optional<TrayCommand> TrayIcon::showContextMenu(
    const bool launcherVisible,
    const std::optional<bool> startupEnabled)
{
    auto menu = createTrayContextMenu(launcherVisible, startupEnabled);
    if (!menu) {
        return std::nullopt;
    }

    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return std::nullopt;
    }
    SetForegroundWindow(data_.hWnd);
    const auto selected = TrackPopupMenuEx(
        menu.get(),
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_WORKAREA,
        cursor.x,
        cursor.y,
        data_.hWnd,
        nullptr);
    PostMessageW(data_.hWnd, WM_NULL, 0, 0);
    if (selected == trayToggleMenuId) {
        return TrayCommand::ToggleLauncher;
    }
    if (selected == trayStartupMenuId) {
        return TrayCommand::ToggleStartup;
    }
    if (selected == traySettingsMenuId) {
        return TrayCommand::Settings;
    }
    if (selected == trayExitMenuId) {
        return TrayCommand::Exit;
    }
    return std::nullopt;
}

} // namespace hlaunch::platform::windows
