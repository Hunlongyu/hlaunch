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

std::optional<std::size_t> hitTestLauncherItem(
    const LauncherLayout& layout,
    const float xDip,
    const float yDip) noexcept
{
    for (std::size_t index = 0; index < layout.items.size(); ++index) {
        if (contains(layout.items[index], xDip, yDip)) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> hitTestLauncherTab(
    const LauncherLayout& layout,
    const std::size_t tabCount,
    const float xDip,
    const float yDip) noexcept
{
    if (tabCount == 0 || !contains(layout.tabs, xDip, yDip)) {
        return std::nullopt;
    }
    const float tabWidth = layout.tabs.width / static_cast<float>(tabCount);
    if (tabWidth <= 0.0F) {
        return std::nullopt;
    }
    return std::min(
        static_cast<std::size_t>((xDip - layout.tabs.x) / tabWidth),
        tabCount - 1U);
}

std::optional<std::size_t> navigateGridItem(
    const std::optional<std::size_t> currentIndex,
    const std::size_t itemCount,
    const std::size_t columns,
    const GridNavigationDirection direction) noexcept
{
    if (itemCount == 0 || columns == 0) {
        return std::nullopt;
    }

    const auto index = std::min(currentIndex.value_or(0), itemCount - 1U);
    switch (direction) {
    case GridNavigationDirection::Left:
        return index % columns == 0 ? index : index - 1U;
    case GridNavigationDirection::Right:
        return index % columns + 1U < columns && index + 1U < itemCount
            ? index + 1U
            : index;
    case GridNavigationDirection::Up:
        return index >= columns ? index - columns : index;
    case GridNavigationDirection::Down:
        return index / columns < (itemCount - 1U) / columns
            ? std::min(index + columns, itemCount - 1U)
            : index;
    case GridNavigationDirection::First:
        return 0U;
    case GridNavigationDirection::Last:
        return itemCount - 1U;
    }
    return index;
}

std::optional<std::size_t> cycleLauncherTab(
    const std::size_t currentIndex,
    const std::size_t tabCount,
    const bool backward) noexcept
{
    if (tabCount == 0) {
        return std::nullopt;
    }
    const auto normalized = std::min(currentIndex, tabCount - 1U);
    if (backward) {
        return normalized == 0 ? tabCount - 1U : normalized - 1U;
    }
    return (normalized + 1U) % tabCount;
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

RectPixels calculateScreenEdgeWindowRectangle(
    const ScreenEdgePlacementRequest& request) noexcept
{
    const auto& workArea = request.workArea;
    const int availableWidth = std::max(0, workArea.right - workArea.left);
    const int availableHeight = std::max(0, workArea.bottom - workArea.top);
    const int width = std::clamp(request.windowWidth, 0, availableWidth);
    const int height = std::clamp(request.windowHeight, 0, availableHeight);
    const int centeredX = std::clamp(
        request.cursorX - (width / 2),
        workArea.left,
        workArea.right - width);
    const int centeredY = std::clamp(
        request.cursorY - (height / 2),
        workArea.top,
        workArea.bottom - height);

    int left = centeredX;
    int top = centeredY;
    switch (request.zone) {
    case core::ScreenEdgeZone::Left:
        left = workArea.left;
        break;
    case core::ScreenEdgeZone::Right:
        left = workArea.right - width;
        break;
    case core::ScreenEdgeZone::Top:
        top = workArea.top;
        break;
    case core::ScreenEdgeZone::Bottom:
        top = workArea.bottom - height;
        break;
    case core::ScreenEdgeZone::TopLeft:
        left = workArea.left;
        top = workArea.top;
        break;
    case core::ScreenEdgeZone::TopRight:
        left = workArea.right - width;
        top = workArea.top;
        break;
    case core::ScreenEdgeZone::BottomLeft:
        left = workArea.left;
        top = workArea.bottom - height;
        break;
    case core::ScreenEdgeZone::BottomRight:
        left = workArea.right - width;
        top = workArea.bottom - height;
        break;
    }
    return RectPixels{left, top, left + width, top + height};
}

} // namespace hlaunch::ui
