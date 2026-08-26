#pragma once

#include "core/data_model.h"

namespace hlaunch::activation {

struct ScreenPoint {
    int x{};
    int y{};

    bool operator==(const ScreenPoint&) const = default;
};

struct ScreenRectangle {
    int left{};
    int top{};
    int right{};
    int bottom{};

    bool operator==(const ScreenRectangle&) const = default;
};

struct ScreenEdgeHit {
    core::ScreenEdgeZone zone{core::ScreenEdgeZone::Left};
    ScreenPoint cursor{};
    ScreenRectangle monitor{};
    ScreenRectangle workArea{};

    bool operator==(const ScreenEdgeHit&) const = default;
};

struct MonitorGeometry {
    ScreenRectangle bounds{};
    ScreenRectangle workArea{};
    unsigned dpi{96};

    bool operator==(const MonitorGeometry&) const = default;
};

} // namespace hlaunch::activation
