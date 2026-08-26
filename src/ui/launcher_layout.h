#pragma once

#include "core/data_model.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hlaunch::ui {

struct RectDip {
    float x{};
    float y{};
    float width{};
    float height{};

    bool operator==(const RectDip&) const = default;
};

struct LauncherMetrics {
    float outerPadding{16.0F};
    float headerHeight{28.0F};
    float sectionGap{12.0F};
    float tabHeight{36.0F};
    float itemWidth{68.0F};
    float itemHeight{84.0F};
    float itemGap{8.0F};
};

struct LauncherLayout {
    RectDip header{};
    RectDip closeButton{};
    RectDip tabs{};
    RectDip grid{};
    RectDip gridContent{};
    std::size_t columns{1};
    std::vector<RectDip> items{};
};

struct LauncherLayoutRequest {
    float clientWidthDip{};
    float clientHeightDip{};
    std::size_t itemCount{};
};

struct SearchPopupMetrics {
    float horizontalPadding{8.0F};
    float verticalPadding{8.0F};
    float fieldHeight{44.0F};
    float gap{6.0F};
};

struct SearchPopupLayout {
    float xOffsetDip{};
    float gapDip{};
    float windowWidthDip{};
    float windowHeightDip{};
    RectDip field{};
};

struct RectPixels {
    int left{};
    int top{};
    int right{};
    int bottom{};
};

struct ScreenEdgePlacementRequest {
    RectPixels workArea{};
    int windowWidth{};
    int windowHeight{};
    int cursorX{};
    int cursorY{};
    core::ScreenEdgeZone zone{core::ScreenEdgeZone::Left};
};

enum class GridNavigationDirection : std::uint8_t {
    Left,
    Right,
    Up,
    Down,
    First,
    Last,
};

[[nodiscard]] LauncherLayout calculateLauncherLayout(
    const LauncherLayoutRequest& request,
    const LauncherMetrics& metrics = {});

[[nodiscard]] SearchPopupLayout calculateSearchPopupLayout(
    const LauncherLayout& launcherLayout,
    const SearchPopupMetrics& metrics = {});

[[nodiscard]] bool isLauncherDragRegion(
    const LauncherLayout& layout,
    float xDip,
    float yDip) noexcept;

[[nodiscard]] std::optional<std::size_t> hitTestLauncherItem(
    const LauncherLayout& layout,
    float xDip,
    float yDip) noexcept;

[[nodiscard]] std::optional<std::size_t> hitTestLauncherTab(
    const LauncherLayout& layout,
    std::size_t tabCount,
    float xDip,
    float yDip) noexcept;

[[nodiscard]] std::optional<std::size_t> navigateGridItem(
    std::optional<std::size_t> currentIndex,
    std::size_t itemCount,
    std::size_t columns,
    GridNavigationDirection direction) noexcept;

[[nodiscard]] std::optional<std::size_t> cycleLauncherTab(
    std::size_t currentIndex,
    std::size_t tabCount,
    bool backward) noexcept;

[[nodiscard]] RectPixels calculateCenteredWindowRectangle(
    const RectPixels& workArea,
    int windowWidth,
    int windowHeight) noexcept;

[[nodiscard]] RectPixels calculateScreenEdgeWindowRectangle(
    const ScreenEdgePlacementRequest& request) noexcept;

} // namespace hlaunch::ui
