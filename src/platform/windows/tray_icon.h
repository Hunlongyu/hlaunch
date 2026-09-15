#pragma once

#include "app_version.h"

#include <Windows.h>
#include <shellapi.h>

#include <wil/resource.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlaunch::platform::windows {

inline constexpr UINT trayIconCallbackMessage = WM_APP + 0x20;
inline constexpr UINT_PTR trayIconRetryTimerId = 0x484C0020;

[[nodiscard]] constexpr std::wstring_view trayIconTooltipText() noexcept
{
    return L"HLaunch\nv" HLAUNCH_VERSION_WIDE_LITERAL;
}

enum class TrayCommand : std::uint8_t {
    ToggleLauncher,
    ToggleStartup,
    Settings,
    Exit,
};

inline constexpr UINT_PTR trayToggleMenuId = 1;
inline constexpr UINT_PTR trayStartupMenuId = 2;
inline constexpr UINT_PTR traySettingsMenuId = 3;
inline constexpr UINT_PTR trayExitMenuId = 4;

[[nodiscard]] wil::unique_hmenu createTrayContextMenu(
    bool launcherVisible,
    std::optional<bool> startupEnabled);

class TrayIcon final {
public:
    using NotifyIconFunction = decltype(&Shell_NotifyIconW);
    explicit TrayIcon(NotifyIconFunction notifyIcon = &Shell_NotifyIconW) noexcept
        : notifyIcon_(notifyIcon ? notifyIcon : &Shell_NotifyIconW) {}
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    TrayIcon(TrayIcon&&) = delete;
    TrayIcon& operator=(TrayIcon&&) = delete;

    // Success means the icon was added or a nonblocking retry was scheduled.
    [[nodiscard]] bool start(HWND owner, HICON icon);
    void stop() noexcept;

    [[nodiscard]] std::optional<TrayCommand> handleMessage(
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        bool launcherVisible,
        std::optional<bool> startupEnabled = std::nullopt);
    [[nodiscard]] bool isAdded() const noexcept;
    [[nodiscard]] UINT taskbarCreatedMessage() const noexcept;

private:
    [[nodiscard]] bool add();
    [[nodiscard]] bool addOrRetry();
    [[nodiscard]] std::optional<TrayCommand> showContextMenu(
        bool launcherVisible,
        std::optional<bool> startupEnabled);

    NOTIFYICONDATAW data_{};
    UINT taskbarCreatedMessage_{};
    bool added_{};
    NotifyIconFunction notifyIcon_;
};

} // namespace hlaunch::platform::windows
