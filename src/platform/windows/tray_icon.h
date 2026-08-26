#pragma once

#include <Windows.h>
#include <shellapi.h>

#include <cstdint>
#include <optional>

namespace hlaunch::platform::windows {

inline constexpr UINT trayIconCallbackMessage = WM_APP + 0x20;

enum class TrayCommand : std::uint8_t {
    ToggleLauncher,
    Exit,
};

class TrayIcon final {
public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    TrayIcon(TrayIcon&&) = delete;
    TrayIcon& operator=(TrayIcon&&) = delete;

    [[nodiscard]] bool start(HWND owner, HICON icon);
    void stop() noexcept;

    [[nodiscard]] std::optional<TrayCommand> handleMessage(
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        bool launcherVisible);
    [[nodiscard]] bool isAdded() const noexcept;
    [[nodiscard]] UINT taskbarCreatedMessage() const noexcept;

private:
    [[nodiscard]] bool add();
    [[nodiscard]] std::optional<TrayCommand> showContextMenu(bool launcherVisible);

    NOTIFYICONDATAW data_{};
    UINT taskbarCreatedMessage_{};
    bool added_{};
};

} // namespace hlaunch::platform::windows
