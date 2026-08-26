#include "ui/launcher_layout.h"

#include <algorithm>
#include <cmath>

namespace hlaunch::ui {
namespace {

bool contains(const RectDip& rectangle, const float xDip, const float yDip) noexcept
{
    return xDip >= rectangle.x
        && xDip < rectangle.x + rectangle.width
        && yDip >= rectangle.y
        && yDip < rectangle.y + rectangle.height;
}

} // namespace

LauncherLayout calculateLauncherLayout(
    const LauncherLayoutRequest& request,
    const LauncherMetrics& metrics)
{
    const float contentWidth = std::max(
        0.0F,
        request.clientWidthDip - (2.0F * metrics.outerPadding));
    LauncherLayout layout{};
    float y = metrics.outerPadding;

    layout.header = RectDip{metrics.outerPadding, y, contentWidth, metrics.headerHeight};
    layout.closeButton = RectDip{
        layout.header.x + std::max(0.0F, layout.header.width - metrics.headerHeight),
        layout.header.y,
        std::min(layout.header.width, metrics.headerHeight),
        metrics.headerHeight,
    };
    y += metrics.headerHeight + metrics.sectionGap;

    const float itemStride = metrics.itemWidth + metrics.itemGap;
    const auto availableColumns = itemStride > 0.0F
        ? static_cast<std::size_t>(std::floor((contentWidth + metrics.itemGap) / itemStride))
        : 1U;
    layout.columns = std::max<std::size_t>(1U, availableColumns);
    const float occupiedWidth = static_cast<float>(layout.columns) * metrics.itemWidth
        + static_cast<float>(layout.columns - 1U) * metrics.itemGap;
    const float itemStartX = metrics.outerPadding
        + std::max(0.0F, (contentWidth - occupiedWidth) / 2.0F);
    const float tabsY = std::max(y, request.clientHeightDip - metrics.tabHeight);
    layout.tabs = RectDip{metrics.outerPadding, tabsY, contentWidth, metrics.tabHeight};
    layout.grid = RectDip{
        metrics.outerPadding,
        y,
        contentWidth,
        std::max(0.0F, tabsY - y - metrics.sectionGap),
    };
    layout.gridContent = RectDip{
        itemStartX,
        layout.grid.y,
        occupiedWidth,
        layout.grid.height,
    };

    layout.items.reserve(request.itemCount);
    for (std::size_t index = 0; index < request.itemCount; ++index) {
        const auto column = index % layout.columns;
        const auto row = index / layout.columns;
        layout.items.push_back(RectDip{
            itemStartX + static_cast<float>(column) * itemStride,
            layout.grid.y + static_cast<float>(row) * (metrics.itemHeight + metrics.itemGap),
            metrics.itemWidth,
            metrics.itemHeight,
        });
    }
    return layout;
}

SearchPopupLayout calculateSearchPopupLayout(
    const LauncherLayout& launcherLayout,
    const SearchPopupMetrics& metrics)
{
    const float windowWidth = launcherLayout.header.width
        + (2.0F * launcherLayout.header.x);
    const float availableFieldWidth = std::max(
        0.0F,
        windowWidth - (2.0F * metrics.horizontalPadding));
    return SearchPopupLayout{
        .xOffsetDip = 0.0F,
        .gapDip = metrics.gap,
        .windowWidthDip = windowWidth,
        .windowHeightDip = metrics.fieldHeight + (2.0F * metrics.verticalPadding),
        .field = RectDip{
            .x = metrics.horizontalPadding,
            .y = metrics.verticalPadding,
            .width = availableFieldWidth,
            .height = metrics.fieldHeight,
        },
    };
}

bool isLauncherDragRegion(
    const LauncherLayout& layout,
    const float xDip,
    const float yDip) noexcept
{
    return !contains(layout.closeButton, xDip, yDip)
        && !contains(layout.grid, xDip, yDip)
        && !contains(layout.tabs, xDip, yDip);
}

RectPixels calculateCenteredWindowRectangle(
    const RectPixels& workArea,
    const int windowWidth,
    const int windowHeight) noexcept
{
    const int availableWidth = std::max(0, workArea.right - workArea.left);
    const int availableHeight = std::max(0, workArea.bottom - workArea.top);
    const int constrainedWidth = std::clamp(windowWidth, 0, availableWidth);
    const int constrainedHeight = std::clamp(windowHeight, 0, availableHeight);
    const int left = workArea.left + ((availableWidth - constrainedWidth) / 2);
    const int top = workArea.top + ((availableHeight - constrainedHeight) / 2);
    return RectPixels{left, top, left + constrainedWidth, top + constrainedHeight};
}

} // namespace hlaunch::ui
