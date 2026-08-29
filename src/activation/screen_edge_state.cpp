#include "activation/screen_edge_state.h"

#include <algorithm>
#include <cmath>

namespace hlaunch::activation {
namespace {

bool contains(const ScreenRectangle& rectangle, const ScreenPoint point) noexcept
{
    return point.x >= rectangle.left && point.x < rectangle.right
        && point.y >= rectangle.top && point.y < rectangle.bottom;
}

bool isZoneEnabled(
    const core::ScreenEdgeConfig& config,
    const core::ScreenEdgeZone zone) noexcept
{
    return std::ranges::find(config.zones, zone) != config.zones.end();
}

bool hasAdjacentMonitor(
    const std::span<const MonitorGeometry> monitors,
    const std::size_t currentIndex,
    const ScreenPoint outsidePoint) noexcept
{
    for (std::size_t index = 0; index < monitors.size(); ++index) {
        if (index != currentIndex && contains(monitors[index].bounds, outsidePoint)) {
            return true;
        }
    }
    return false;
}

int dipToPixels(const double dip, const unsigned dpi) noexcept
{
    return std::max(1, static_cast<int>(std::ceil(dip * static_cast<double>(dpi) / 96.0)));
}

} // namespace

std::optional<ScreenEdgeHit> detectScreenEdge(
    const ScreenPoint cursor,
    const std::span<const MonitorGeometry> monitors,
    const core::ScreenEdgeConfig& config)
{
    if (!config.enabled) {
        return std::nullopt;
    }

    std::size_t monitorIndex = monitors.size();
    for (std::size_t index = 0; index < monitors.size(); ++index) {
        if (contains(monitors[index].bounds, cursor)) {
            monitorIndex = index;
            break;
        }
    }
    if (monitorIndex == monitors.size()) {
        return std::nullopt;
    }

    const auto& monitor = monitors[monitorIndex];
    const int thickness = dipToPixels(config.thicknessDip, monitor.dpi);
    const int cornerSize = dipToPixels(config.cornerSizeDip, monitor.dpi);
    bool leftExposed = true;
    bool rightExposed = true;
    bool topExposed = true;
    bool bottomExposed = true;

    if (config.edgeMode == core::ScreenEdgeMode::DesktopOuter) {
        leftExposed = !hasAdjacentMonitor(
            monitors,
            monitorIndex,
            ScreenPoint{monitor.bounds.left - 1, cursor.y});
        rightExposed = !hasAdjacentMonitor(
            monitors,
            monitorIndex,
            ScreenPoint{monitor.bounds.right, cursor.y});
        topExposed = !hasAdjacentMonitor(
            monitors,
            monitorIndex,
            ScreenPoint{cursor.x, monitor.bounds.top - 1});
        bottomExposed = !hasAdjacentMonitor(
            monitors,
            monitorIndex,
            ScreenPoint{cursor.x, monitor.bounds.bottom});
    }

    const bool left = leftExposed && cursor.x < monitor.bounds.left + thickness;
    const bool right = rightExposed && cursor.x >= monitor.bounds.right - thickness;
    const bool top = topExposed && cursor.y < monitor.bounds.top + thickness;
    const bool bottom = bottomExposed && cursor.y >= monitor.bounds.bottom - thickness;
    const bool nearLeftCorner = cursor.x < monitor.bounds.left + cornerSize;
    const bool nearRightCorner = cursor.x >= monitor.bounds.right - cornerSize;
    const bool nearTopCorner = cursor.y < monitor.bounds.top + cornerSize;
    const bool nearBottomCorner = cursor.y >= monitor.bounds.bottom - cornerSize;

    const auto makeHit = [&](const core::ScreenEdgeZone zone) {
        return ScreenEdgeHit{zone, cursor, monitor.bounds, monitor.workArea};
    };
    if (leftExposed && topExposed && nearLeftCorner && nearTopCorner
        && isZoneEnabled(config, core::ScreenEdgeZone::TopLeft)) {
        return makeHit(core::ScreenEdgeZone::TopLeft);
    }
    if (rightExposed && topExposed && nearRightCorner && nearTopCorner
        && isZoneEnabled(config, core::ScreenEdgeZone::TopRight)) {
        return makeHit(core::ScreenEdgeZone::TopRight);
    }
    if (leftExposed && bottomExposed && nearLeftCorner && nearBottomCorner
        && isZoneEnabled(config, core::ScreenEdgeZone::BottomLeft)) {
        return makeHit(core::ScreenEdgeZone::BottomLeft);
    }
    if (rightExposed && bottomExposed && nearRightCorner && nearBottomCorner
        && isZoneEnabled(config, core::ScreenEdgeZone::BottomRight)) {
        return makeHit(core::ScreenEdgeZone::BottomRight);
    }
    if (left && isZoneEnabled(config, core::ScreenEdgeZone::Left)) {
        return makeHit(core::ScreenEdgeZone::Left);
    }
    if (right && isZoneEnabled(config, core::ScreenEdgeZone::Right)) {
        return makeHit(core::ScreenEdgeZone::Right);
    }
    if (top && isZoneEnabled(config, core::ScreenEdgeZone::Top)) {
        return makeHit(core::ScreenEdgeZone::Top);
    }
    if (bottom && isZoneEnabled(config, core::ScreenEdgeZone::Bottom)) {
        return makeHit(core::ScreenEdgeZone::Bottom);
    }
    return std::nullopt;
}

EdgeDwellStateMachine::EdgeDwellStateMachine(const EdgeDwellTiming timing) noexcept
    : dwellMs_(timing.dwellMs), cooldownMs_(timing.cooldownMs)
{
}

std::optional<ScreenEdgeHit> EdgeDwellStateMachine::update(
    std::optional<ScreenEdgeHit> hit,
    const bool suppressed,
    const std::uint64_t nowMs) noexcept
{
    if (phase_ == EdgeDwellPhase::Idle) {
        if (!suppressed && hit) {
            phase_ = EdgeDwellPhase::Pending;
            activeHit_ = hit;
            phaseStartedAt_ = nowMs;
        }
        return std::nullopt;
    }

    if (phase_ == EdgeDwellPhase::Pending) {
        if (suppressed || !hit) {
            reset();
            return std::nullopt;
        }
        if (!activeHit_ || hit->zone != activeHit_->zone || hit->monitor != activeHit_->monitor) {
            activeHit_ = hit;
            phaseStartedAt_ = nowMs;
            return std::nullopt;
        }
        activeHit_ = hit;
        if (nowMs < phaseStartedAt_) {
            phaseStartedAt_ = nowMs;
            return std::nullopt;
        }
        if (nowMs - phaseStartedAt_ >= dwellMs_) {
            phase_ = EdgeDwellPhase::Triggered;
            phaseStartedAt_ = nowMs;
            leftAfterTrigger_ = false;
            return activeHit_;
        }
        return std::nullopt;
    }

    if (!hit || !activeHit_ || hit->zone != activeHit_->zone
        || hit->monitor != activeHit_->monitor) {
        leftAfterTrigger_ = true;
    }
    if (nowMs < phaseStartedAt_) {
        phaseStartedAt_ = nowMs;
        return std::nullopt;
    }
    if (leftAfterTrigger_ && nowMs - phaseStartedAt_ >= cooldownMs_) {
        if (!suppressed && hit) {
            phase_ = EdgeDwellPhase::Pending;
            activeHit_ = hit;
            phaseStartedAt_ = nowMs;
            leftAfterTrigger_ = false;
        }
        else {
            reset();
        }
    }
    return std::nullopt;
}

void EdgeDwellStateMachine::reset() noexcept
{
    phase_ = EdgeDwellPhase::Idle;
    activeHit_.reset();
    phaseStartedAt_ = 0;
    leftAfterTrigger_ = false;
}

EdgeDwellPhase EdgeDwellStateMachine::phase() const noexcept
{
    return phase_;
}

} // namespace hlaunch::activation
