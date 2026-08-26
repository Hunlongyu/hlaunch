#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "activation/screen_edge_state.h"
#include "ui/launcher_layout.h"

#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <vector>

namespace {

hlaunch::core::ScreenEdgeConfig enabledConfig()
{
    auto config = hlaunch::core::ScreenEdgeConfig{};
    config.enabled = true;
    config.edgeMode = hlaunch::core::ScreenEdgeMode::EveryMonitor;
    config.zones = {
        hlaunch::core::ScreenEdgeZone::Left,
        hlaunch::core::ScreenEdgeZone::Right,
        hlaunch::core::ScreenEdgeZone::Top,
        hlaunch::core::ScreenEdgeZone::Bottom,
        hlaunch::core::ScreenEdgeZone::TopLeft,
        hlaunch::core::ScreenEdgeZone::TopRight,
        hlaunch::core::ScreenEdgeZone::BottomLeft,
        hlaunch::core::ScreenEdgeZone::BottomRight,
    };
    return config;
}

hlaunch::activation::MonitorGeometry monitor(
    const int left,
    const int top,
    const int right,
    const int bottom,
    const unsigned dpi = 96)
{
    return {
        {left, top, right, bottom},
        {left, top, right, bottom},
        dpi,
    };
}

hlaunch::activation::ScreenEdgeHit hit(
    const hlaunch::core::ScreenEdgeZone zone,
    const int monitorLeft = 0)
{
    return {
        zone,
        {monitorLeft, 50},
        {monitorLeft, 0, monitorLeft + 100, 100},
        {monitorLeft, 0, monitorLeft + 100, 100},
    };
}

} // namespace

TEST_CASE("ACT-EDGE-001 remains disabled with the product defaults")
{
    const std::array monitors{monitor(0, 0, 1920, 1080)};
    CHECK_FALSE(hlaunch::activation::detectScreenEdge(
        {0, 500},
        monitors,
        hlaunch::core::ScreenEdgeConfig{}).has_value());
}

TEST_CASE("ACT-EDGE-001 detects all zones with corner priority and exclusive bounds")
{
    const std::array monitors{monitor(-1920, -200, 0, 880)};
    const auto config = enabledConfig();
    const std::array cases{
        std::pair{hlaunch::activation::ScreenPoint{-1920, 300}, hlaunch::core::ScreenEdgeZone::Left},
        std::pair{hlaunch::activation::ScreenPoint{-1, 300}, hlaunch::core::ScreenEdgeZone::Right},
        std::pair{hlaunch::activation::ScreenPoint{-900, -200}, hlaunch::core::ScreenEdgeZone::Top},
        std::pair{hlaunch::activation::ScreenPoint{-900, 879}, hlaunch::core::ScreenEdgeZone::Bottom},
        std::pair{hlaunch::activation::ScreenPoint{-1920, -200}, hlaunch::core::ScreenEdgeZone::TopLeft},
        std::pair{hlaunch::activation::ScreenPoint{-1, -200}, hlaunch::core::ScreenEdgeZone::TopRight},
        std::pair{hlaunch::activation::ScreenPoint{-1920, 879}, hlaunch::core::ScreenEdgeZone::BottomLeft},
        std::pair{hlaunch::activation::ScreenPoint{-1, 879}, hlaunch::core::ScreenEdgeZone::BottomRight},
    };
    for (const auto& [point, expected] : cases) {
        const auto detected = hlaunch::activation::detectScreenEdge(point, monitors, config);
        CHECK(detected.has_value());
        if (detected) {
            CHECK(detected->zone == expected);
        }
    }
    const auto insideFullCornerArea = hlaunch::activation::detectScreenEdge(
        {-1910, -190},
        monitors,
        config);
    CHECK(insideFullCornerArea.has_value());
    if (insideFullCornerArea) {
        CHECK(insideFullCornerArea->zone == hlaunch::core::ScreenEdgeZone::TopLeft);
    }
    CHECK_FALSE(hlaunch::activation::detectScreenEdge({0, 300}, monitors, config).has_value());
    CHECK_FALSE(hlaunch::activation::detectScreenEdge({-900, 880}, monitors, config).has_value());
}

TEST_CASE("ACT-EDGE-001 converts DIP thickness with the target monitor DPI")
{
    const std::array monitors{monitor(0, 0, 100, 100, 144)};
    auto config = enabledConfig();
    config.zones = {hlaunch::core::ScreenEdgeZone::Left};

    CHECK(hlaunch::activation::detectScreenEdge({5, 50}, monitors, config).has_value());
    CHECK_FALSE(hlaunch::activation::detectScreenEdge({6, 50}, monitors, config).has_value());
}

TEST_CASE("ACT-EDGE-001 desktop outer mode rejects seams and accepts exposed L-shape segments")
{
    const std::array monitors{
        monitor(0, 0, 100, 200),
        monitor(100, 0, 200, 100),
    };
    auto config = enabledConfig();
    config.edgeMode = hlaunch::core::ScreenEdgeMode::DesktopOuter;
    config.zones = {hlaunch::core::ScreenEdgeZone::Right};

    CHECK_FALSE(hlaunch::activation::detectScreenEdge({99, 50}, monitors, config).has_value());
    const auto exposed = hlaunch::activation::detectScreenEdge({99, 150}, monitors, config);
    CHECK(exposed.has_value());
    if (exposed) {
        CHECK(exposed->zone == hlaunch::core::ScreenEdgeZone::Right);
    }
}

TEST_CASE("ACT-EDGE-001 dwell requires leave and cooldown before rearming")
{
    using hlaunch::activation::EdgeDwellPhase;
    hlaunch::activation::EdgeDwellStateMachine state{{300, 500}};
    const auto left = hit(hlaunch::core::ScreenEdgeZone::Left);

    CHECK_FALSE(state.update(left, false, 1'000).has_value());
    CHECK(state.phase() == EdgeDwellPhase::Pending);
    CHECK_FALSE(state.update(left, false, 1'299).has_value());
    REQUIRE(state.update(left, false, 1'300).has_value());
    CHECK(state.phase() == EdgeDwellPhase::Triggered);
    CHECK_FALSE(state.update(left, false, 2'000).has_value());
    CHECK_FALSE(state.update(std::nullopt, false, 2'000).has_value());
    CHECK(state.phase() == EdgeDwellPhase::Idle);

    CHECK_FALSE(state.update(left, false, 2'001).has_value());
    REQUIRE(state.update(left, false, 2'301).has_value());
}

TEST_CASE("ACT-EDGE-001 switching zones, suppression and clock rollback restart dwell")
{
    using hlaunch::activation::EdgeDwellPhase;
    hlaunch::activation::EdgeDwellStateMachine state{{300, 500}};
    const auto left = hit(hlaunch::core::ScreenEdgeZone::Left);
    const auto top = hit(hlaunch::core::ScreenEdgeZone::Top);

    CHECK_FALSE(state.update(left, false, 1'000).has_value());
    CHECK_FALSE(state.update(top, false, 1'250).has_value());
    CHECK_FALSE(state.update(top, false, 1'500).has_value());
    REQUIRE(state.update(top, false, 1'550).has_value());

    state.reset();
    CHECK_FALSE(state.update(left, false, 2'000).has_value());
    CHECK_FALSE(state.update(left, true, 2'250).has_value());
    CHECK(state.phase() == EdgeDwellPhase::Idle);

    CHECK_FALSE(state.update(left, false, 3'000).has_value());
    CHECK_FALSE(state.update(left, false, 2'900).has_value());
    CHECK_FALSE(state.update(left, false, 3'199).has_value());
    REQUIRE(state.update(left, false, 3'200).has_value());
}

TEST_CASE("ACT-EDGE-001 edge placement anchors and constrains the launcher")
{
    using hlaunch::core::ScreenEdgeZone;
    const hlaunch::ui::RectPixels work{-1920, 0, 0, 1040};
    const auto left = hlaunch::ui::calculateScreenEdgeWindowRectangle({
        work, 420, 640, -1920, 800, ScreenEdgeZone::Left,
    });
    const auto bottomRight = hlaunch::ui::calculateScreenEdgeWindowRectangle({
        work, 420, 640, -1, 1039, ScreenEdgeZone::BottomRight,
    });
    const auto oversized = hlaunch::ui::calculateScreenEdgeWindowRectangle({
        {100, 50, 400, 250}, 420, 640, 100, 50, ScreenEdgeZone::TopLeft,
    });

    CHECK(left.left == -1920);
    CHECK(left.top == 400);
    CHECK(bottomRight.left == -420);
    CHECK(bottomRight.top == 400);
    CHECK(oversized.left == 100);
    CHECK(oversized.top == 50);
    CHECK(oversized.right == 400);
    CHECK(oversized.bottom == 250);
}
