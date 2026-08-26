#include "platform/windows/tray_icon.h"

#include <wil/resource.h>

#include <algorithm>
#include <iterator>

namespace hlaunch::platform::windows {
namespace {

constexpr UINT_PTR toggleMenuId = 1;
constexpr UINT_PTR settingsMenuId = 2;
constexpr UINT_PTR exitMenuId = 3;

} // namespace

TrayIcon::~TrayIcon()
{
    stop();
}

bool TrayIcon::start(const HWND owner, const HICON icon)
{
    stop();
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
    constexpr wchar_t tooltip[] = L"HLaunch";
    static_assert(
        std::size(tooltip) <= sizeof(NOTIFYICONDATAW::szTip) / sizeof(wchar_t));
    std::ranges::copy(tooltip, data_.szTip);
    return add();
}

void TrayIcon::stop() noexcept
{
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
    }
    data_ = {};
    taskbarCreatedMessage_ = 0;
    added_ = false;
}

std::optional<TrayCommand> TrayIcon::handleMessage(
    const UINT message,
    WPARAM,
    const LPARAM lParam,
    const bool launcherVisible)
{
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        added_ = false;
        static_cast<void>(add());
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
        return showContextMenu(launcherVisible);
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
    if (!Shell_NotifyIconW(NIM_ADD, &data_)) {
        added_ = false;
        return false;
    }
    data_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data_);
    added_ = true;
    return true;
}

std::optional<TrayCommand> TrayIcon::showContextMenu(const bool launcherVisible)
{
    wil::unique_hmenu menu{CreatePopupMenu()};
    if (!menu) {
        return std::nullopt;
    }

    AppendMenuW(
        menu.get(),
        MF_STRING,
        toggleMenuId,
        launcherVisible ? L"隐藏 HLaunch" : L"显示 HLaunch");
    AppendMenuW(menu.get(), MF_STRING | MF_GRAYED, settingsMenuId, L"设置（尚未实现）");
    AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu.get(), MF_STRING, exitMenuId, L"退出");

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
    if (selected == toggleMenuId) {
        return TrayCommand::ToggleLauncher;
    }
    if (selected == exitMenuId) {
        return TrayCommand::Exit;
    }
    return std::nullopt;
}

} // namespace hlaunch::platform::windows
