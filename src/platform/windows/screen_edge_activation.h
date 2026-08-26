#pragma once

#include "activation/screen_edge_state.h"
#include "core/data_model.h"

#include <Windows.h>
#include <wil/resource.h>

#include <mutex>
#include <optional>
#include <vector>

namespace hlaunch::platform::windows {

inline constexpr UINT screenEdgeActivationMessage = WM_APP + 0x21;

struct ScreenEdgeActivationTargets {
    HWND activationWindow{};
    HWND launcherWindow{};
};

class ScreenEdgeActivation final {
public:
    ScreenEdgeActivation() = default;
    ~ScreenEdgeActivation();

    ScreenEdgeActivation(const ScreenEdgeActivation&) = delete;
    ScreenEdgeActivation& operator=(const ScreenEdgeActivation&) = delete;
    ScreenEdgeActivation(ScreenEdgeActivation&&) = delete;
    ScreenEdgeActivation& operator=(ScreenEdgeActivation&&) = delete;

    [[nodiscard]] bool start(
        ScreenEdgeActivationTargets targets,
        const core::ScreenEdgeConfig& config);
    void stop() noexcept;
    void refreshMonitors();

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] std::optional<activation::ScreenEdgeHit> takePendingActivation();

private:
    static void CALLBACK timerCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER);
    void sample() noexcept;

    mutable std::mutex mutex_{};
    HWND activationWindow_{};
    HWND launcherWindow_{};
    core::ScreenEdgeConfig config_{};
    std::vector<activation::MonitorGeometry> monitors_{};
    activation::EdgeDwellStateMachine state_{{300, 500}};
    std::optional<activation::ScreenEdgeHit> pendingActivation_{};
    wil::unique_threadpool_timer timer_{};
};

} // namespace hlaunch::platform::windows
