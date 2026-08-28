#include "platform/windows/screen_edge_activation.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace hlaunch::platform::windows {
namespace {

activation::ScreenRectangle toScreenRectangle(const RECT& rectangle) noexcept
{
    return {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
}

BOOL CALLBACK collectMonitor(
    const HMONITOR monitor,
    HDC,
    RECT*,
    const LPARAM data) noexcept
{
    try {
    auto* monitors = reinterpret_cast<std::vector<activation::MonitorGeometry>*>(data); // NOLINT(performance-no-int-to-ptr): EnumDisplayMonitors carries caller context in LPARAM.
    MONITORINFO info{};
    info.cbSize = sizeof(MONITORINFO);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }

    UINT dpiX = 96;
    UINT dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        dpiX = 96;
    }
    monitors->push_back(activation::MonitorGeometry{
        toScreenRectangle(info.rcMonitor),
        toScreenRectangle(info.rcWork),
        dpiX,
    });
    return TRUE;
    } catch (...) { return FALSE; }
}

std::vector<activation::MonitorGeometry> enumerateMonitors()
{
    std::vector<activation::MonitorGeometry> monitors{};
    EnumDisplayMonitors(
        nullptr,
        nullptr,
        &collectMonitor,
        reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

bool approximatelyEqual(const int left, const int right) noexcept
{
    return std::abs(left - right) <= 2;
}

bool isFullscreenWindowOnMonitor(
    const HWND foreground,
    const HWND launcherWindow,
    const activation::ScreenRectangle& monitor) noexcept
{
    if (!foreground || foreground == launcherWindow || foreground == GetShellWindow()
        || foreground == GetDesktopWindow() || !IsWindowVisible(foreground)
        || IsIconic(foreground)) {
        return false;
    }

    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(
            foreground,
            DWMWA_CLOAKED,
            &cloaked,
            sizeof(cloaked))) && cloaked) {
        return false;
    }

    RECT bounds{};
    if (FAILED(DwmGetWindowAttribute(
            foreground,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &bounds,
            sizeof(bounds))) && !GetWindowRect(foreground, &bounds)) {
        return false;
    }
    return approximatelyEqual(bounds.left, monitor.left)
        && approximatelyEqual(bounds.top, monitor.top)
        && approximatelyEqual(bounds.right, monitor.right)
        && approximatelyEqual(bounds.bottom, monitor.bottom);
}

} // namespace

ScreenEdgeActivation::~ScreenEdgeActivation()
{
    stop();
}

bool ScreenEdgeActivation::start(
    const ScreenEdgeActivationTargets targets,
    const core::ScreenEdgeConfig& config)
{
    stop();
    if (!config.enabled) {
        return true;
    }

    auto monitors = enumerateMonitors();
    if (monitors.empty()) {
        return false;
    }

    {
        const std::scoped_lock lock{mutex_};
        activationWindow_ = targets.activationWindow;
        launcherWindow_ = targets.launcherWindow;
        config_ = config;
        foregroundProcessFilter_ = ForegroundProcessFilter{config};
        cachedForegroundWindow_ = nullptr;
        cachedForegroundSuppressed_ = false;
        monitors_ = std::move(monitors);
        state_ = activation::EdgeDwellStateMachine{{config.dwellMs, config.cooldownMs}};
    }

    timer_.reset(CreateThreadpoolTimer(&ScreenEdgeActivation::timerCallback, this, nullptr));
    if (!timer_) {
        stop();
        return false;
    }

    LARGE_INTEGER relativeDueTime{};
    relativeDueTime.QuadPart = -10'000;
    FILETIME dueTime{
        relativeDueTime.LowPart,
        static_cast<DWORD>(relativeDueTime.HighPart),
    };
    SetThreadpoolTimer(timer_.get(), &dueTime, config.pollMs, 0);
    return true;
}

void ScreenEdgeActivation::stop() noexcept
{
    timer_.reset();
    const std::scoped_lock lock{mutex_};
    activationWindow_ = nullptr;
    launcherWindow_ = nullptr;
    foregroundProcessFilter_ = ForegroundProcessFilter{};
    cachedForegroundWindow_ = nullptr;
    cachedForegroundSuppressed_ = false;
    monitors_.clear();
    pendingActivation_.reset();
    state_.reset();
}

void ScreenEdgeActivation::refreshMonitors()
{
    auto monitors = enumerateMonitors();
    const std::scoped_lock lock{mutex_};
    monitors_ = std::move(monitors);
    state_.reset();
}

bool ScreenEdgeActivation::isRunning() const noexcept
{
    return static_cast<bool>(timer_);
}

std::optional<activation::ScreenEdgeHit> ScreenEdgeActivation::takePendingActivation()
{
    const std::scoped_lock lock{mutex_};
    return std::exchange(pendingActivation_, std::nullopt);
}

void CALLBACK ScreenEdgeActivation::timerCallback(
    PTP_CALLBACK_INSTANCE,
    void* const context,
    PTP_TIMER) noexcept
{
    static_cast<ScreenEdgeActivation*>(context)->sample();
}

void ScreenEdgeActivation::sample() noexcept
{
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }

    const std::scoped_lock lock{mutex_};
    const auto hit = activation::detectScreenEdge(
        activation::ScreenPoint{cursor.x, cursor.y},
        monitors_,
        config_);
    const HWND foreground = hit ? GetForegroundWindow() : nullptr;
    if (hit && foregroundProcessFilter_.active() && foreground != cachedForegroundWindow_) {
        cachedForegroundWindow_ = foreground;
        if (!foreground || foreground == launcherWindow_ || foreground == GetShellWindow()
            || foreground == GetDesktopWindow()) {
            cachedForegroundSuppressed_ = false;
        }
        else {
            DWORD foregroundProcessId{};
            GetWindowThreadProcessId(foreground, &foregroundProcessId);
            if (foregroundProcessId == GetCurrentProcessId()) {
                cachedForegroundSuppressed_ = false;
            }
            else {
                const auto executableName = executableNameForWindow(foreground);
                cachedForegroundSuppressed_ = foregroundProcessFilter_.suppresses(
                    executableName
                        ? std::optional<std::wstring_view>{*executableName}
                        : std::nullopt);
            }
        }
    }
    const bool suppressed = hit
        && (cachedForegroundSuppressed_
            || (config_.disableOnFullscreen
                && isFullscreenWindowOnMonitor(foreground, launcherWindow_, hit->monitor)));
    auto trigger = state_.update(hit, suppressed, GetTickCount64());
    if (!trigger || pendingActivation_) {
        return;
    }

    pendingActivation_ = trigger;
    if (!PostMessageW(activationWindow_, screenEdgeActivationMessage, 0, 0)) {
        pendingActivation_.reset();
    }
}

} // namespace hlaunch::platform::windows
