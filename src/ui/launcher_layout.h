#pragma once

#include <cstddef>
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

[[nodiscard]] RectPixels calculateCenteredWindowRectangle(
    const RectPixels& workArea,
    int windowWidth,
    int windowHeight) noexcept;

} // namespace hlaunch::ui
