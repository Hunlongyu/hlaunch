#pragma once

#include <Windows.h>
#include <wil/resource.h>

#include <optional>

namespace hlaunch::platform::windows {

inline constexpr UINT popupForegroundChangedMessage = WM_APP + 0x45U;

// A popup, its child controls and its owned dialogs form one interaction family.
[[nodiscard]] bool belongsToPopup(HWND candidate, HWND popup) noexcept;

// Created, started, stopped and consumed on the owning UI thread. Only observes
// input while the popup is visible; never captures or consumes another app's click.
class PopupInputMonitor final {
public:
    PopupInputMonitor() = default;
    ~PopupInputMonitor();
    PopupInputMonitor(const PopupInputMonitor&) = delete;
    PopupInputMonitor& operator=(const PopupInputMonitor&) = delete;

    void start(HWND target);
    void stop() noexcept;
    [[nodiscard]] bool acceptsForegroundEvent(WPARAM token) const noexcept;
    [[nodiscard]] std::optional<POINT> pointerDown(HRAWINPUT input) const noexcept;

private:
    wil::unique_hwineventhook foregroundHook_{};
    HWND target_{};
    UINT_PTR token_{};
    DWORD startedAt_{};
    bool mouseRegistered_{};
};

} // namespace hlaunch::platform::windows
