#include "platform/windows/popup_input_monitor.h"

#include "infrastructure/logging/diagnostic_log.h"

#include <windowsx.h>

#include <cstdint>
#include <unordered_map>

namespace hlaunch::platform::windows {
namespace {

struct HookTarget {
    HWND window{};
    UINT_PTR token{};
    DWORD startedAt{};
};

// OUTOFCONTEXT callbacks are delivered on the thread that registered the hook.
thread_local std::unordered_map<HWINEVENTHOOK, HookTarget> hookTargets;
thread_local UINT_PTR nextToken{};

void CALLBACK foregroundChanged(
    HWINEVENTHOOK hook, DWORD, HWND, LONG, LONG, DWORD, DWORD eventTime) noexcept
{
    const auto found = hookTargets.find(hook);
    if (found != hookTargets.end()
        && static_cast<std::int32_t>(eventTime - found->second.startedAt) >= 0) {
        PostMessageW(found->second.window, popupForegroundChangedMessage,
                     found->second.token, 0);
    }
}

} // namespace

bool belongsToPopup(HWND candidate, const HWND popup) noexcept
{
    if (!popup || !candidate) {
        return false;
    }
    candidate = GetAncestor(candidate, GA_ROOT);
    for (unsigned depth = 0; candidate && depth < 64; ++depth) {
        if (candidate == popup) {
            return true;
        }
        candidate = GetWindow(candidate, GW_OWNER);
    }
    return false;
}

PopupInputMonitor::~PopupInputMonitor()
{
    stop();
}

void PopupInputMonitor::start(const HWND target)
{
    if (target_ == target) {
        return;
    }
    stop();
    target_ = target;
    startedAt_ = GetTickCount();
    token_ = ++nextToken;
    RAWINPUTDEVICE mouse{0x01, 0x02, RIDEV_INPUTSINK, target};
    mouseRegistered_ = RegisterRawInputDevices(&mouse, 1, sizeof(mouse)) != FALSE;
    if (!mouseRegistered_) {
        infrastructure::logging::writeSystemError(infrastructure::logging::Level::Warning,
            "popup_mouse_observer_failed", GetLastError());
    }
    foregroundHook_.reset(SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, &foregroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT));
    if (foregroundHook_) {
        hookTargets.emplace(foregroundHook_.get(), HookTarget{target, token_, startedAt_});
    }
    else {
        infrastructure::logging::writeSystemError(infrastructure::logging::Level::Warning,
            "popup_foreground_observer_failed", GetLastError());
    }
}

void PopupInputMonitor::stop() noexcept
{
    if (foregroundHook_) {
        hookTargets.erase(foregroundHook_.get());
        foregroundHook_.reset();
    }
    if (mouseRegistered_) {
        RAWINPUTDEVICE mouse{0x01, 0x02, RIDEV_REMOVE, nullptr};
        if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse))) {
            infrastructure::logging::writeSystemError(infrastructure::logging::Level::Warning,
                "popup_mouse_observer_stop_failed", GetLastError());
        }
    }
    mouseRegistered_ = false;
    target_ = nullptr;
    token_ = 0;
}

bool PopupInputMonitor::acceptsForegroundEvent(const WPARAM token) const noexcept
{
    return target_ && token_ != 0 && token == token_;
}

std::optional<POINT> PopupInputMonitor::pointerDown(const HRAWINPUT input) const noexcept
{
    if (!mouseRegistered_
        || static_cast<std::int32_t>(static_cast<DWORD>(GetMessageTime()) - startedAt_) < 0) {
        return std::nullopt;
    }
    RAWINPUT data{};
    UINT size = sizeof(data);
    if (GetRawInputData(input, RID_INPUT, &data, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)
        || data.header.dwType != RIM_TYPEMOUSE) {
        return std::nullopt;
    }
    constexpr USHORT buttonDown = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN
        | RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_5_DOWN;
    if ((data.data.mouse.usButtonFlags & buttonDown) == 0) {
        return std::nullopt;
    }
    const DWORD position = GetMessagePos();
    return POINT{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
}

} // namespace hlaunch::platform::windows
