#pragma once

#include "activation/activation_context.h"
#include "core/data_model.h"

#include <cstdint>
#include <optional>
#include <span>

namespace hlaunch::activation {

[[nodiscard]] std::optional<ScreenEdgeHit> detectScreenEdge(
    ScreenPoint cursor,
    std::span<const MonitorGeometry> monitors,
    const core::ScreenEdgeConfig& config);

enum class EdgeDwellPhase : std::uint8_t {
    Idle,
    Pending,
    Triggered,
};

struct EdgeDwellTiming {
    std::uint32_t dwellMs{};
    std::uint32_t cooldownMs{};
};

class EdgeDwellStateMachine final {
public:
    explicit EdgeDwellStateMachine(EdgeDwellTiming timing) noexcept;

    [[nodiscard]] std::optional<ScreenEdgeHit> update(
        std::optional<ScreenEdgeHit> hit,
        bool suppressed,
        std::uint64_t nowMs) noexcept;
    void reset() noexcept;

    [[nodiscard]] EdgeDwellPhase phase() const noexcept;

private:
    std::uint32_t dwellMs_{};
    std::uint32_t cooldownMs_{};
    EdgeDwellPhase phase_{EdgeDwellPhase::Idle};
    std::optional<ScreenEdgeHit> activeHit_{};
    std::uint64_t phaseStartedAt_{};
    bool leftAfterTrigger_{};
};

} // namespace hlaunch::activation
