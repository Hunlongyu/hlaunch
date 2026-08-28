#include "ui/launcher_layout.h"

#include <algorithm>
#include <cmath>

namespace hlaunch::ui {
namespace {

constexpr float defaultLauncherWidthDip = 394.0F;
constexpr float defaultLauncherHeightDip = 590.0F;
constexpr float defaultHeaderHeightDip = 16.0F;
constexpr float defaultItemWidthDip = 72.0F;
constexpr float defaultItemHeightDip = 62.0F;
constexpr float defaultItemGapDip = 4.0F;
constexpr float defaultSectionGapDip = 4.0F;
constexpr float defaultGridTabGapDip = 8.0F;
constexpr float defaultTabHeightDip = 30.0F;

bool contains(const RectDip& rectangle, const float xDip, const float yDip) noexcept
{
    return xDip >= rectangle.x
        && xDip < rectangle.x + rectangle.width
        && yDip >= rectangle.y
        && yDip < rectangle.y + rectangle.height;
}

} // namespace

RectDip insetRectForInsideStroke(
    const RectDip& bounds,
    const float strokeWidth) noexcept
{
    const auto maximumInset = std::max(
        0.0F, std::min(bounds.width, bounds.height) / 2.0F);
    const auto inset = std::clamp(
        std::max(0.0F, strokeWidth) / 2.0F,
        0.0F,
        maximumInset);
    return {
        .x = bounds.x + inset,
        .y = bounds.y + inset,
        .width = std::max(0.0F, bounds.width - 2.0F * inset),
        .height = std::max(0.0F, bounds.height - 2.0F * inset),
    };
}

std::uint32_t iconPixelSizeForDpi(
    const float iconSizeDip,
    const std::uint32_t dpi) noexcept
{
    const auto effectiveDpi = std::max<std::uint32_t>(dpi, 1U);
    const auto pixels = static_cast<long>(std::lround(
        std::max(1.0F, iconSizeDip) * static_cast<float>(effectiveDpi) / 96.0F));
    return static_cast<std::uint32_t>(std::clamp(pixels, 16L, 256L));
}

SizeDip calculateLauncherWindowSizeDip(
    const LauncherGridSize gridSize,
    const LauncherMetrics& metrics) noexcept
{
    const auto columns = std::clamp(
        gridSize.columns,
        static_cast<std::size_t>(core::minimumLauncherGridColumns),
        static_cast<std::size_t>(core::maximumLauncherGridColumns));
    const auto rows = std::clamp(
        gridSize.rows,
        static_cast<std::size_t>(core::minimumLauncherGridRows),
        static_cast<std::size_t>(core::maximumLauncherGridRows));
    const auto widthDelta = static_cast<float>(core::defaultLauncherGridColumns)
            * (metrics.itemWidth - defaultItemWidthDip)
        + static_cast<float>(core::defaultLauncherGridColumns - 1U)
            * (metrics.itemGap - defaultItemGapDip);
    const auto heightDelta = static_cast<float>(core::defaultLauncherGridRows)
            * (metrics.itemHeight - defaultItemHeightDip)
        + static_cast<float>(core::defaultLauncherGridRows - 1U)
            * (metrics.itemGap - defaultItemGapDip)
        + (metrics.headerHeight - defaultHeaderHeightDip)
        + (metrics.sectionGap - defaultSectionGapDip)
        + (metrics.gridTabGap - defaultGridTabGapDip)
        + (metrics.tabHeight - defaultTabHeightDip);
    return {
        defaultLauncherWidthDip
            + static_cast<float>(static_cast<std::ptrdiff_t>(columns)
                - static_cast<std::ptrdiff_t>(core::defaultLauncherGridColumns))
                * (metrics.itemWidth + metrics.itemGap)
            + widthDelta,
        defaultLauncherHeightDip
            + static_cast<float>(static_cast<std::ptrdiff_t>(rows)
                - static_cast<std::ptrdiff_t>(core::defaultLauncherGridRows))
                * (metrics.itemHeight + metrics.itemGap)
            + heightDelta,
    };
}

LauncherGridSize calculateLauncherGridSize(
    const float clientWidthDip,
    const float clientHeightDip,
    const LauncherMetrics& metrics) noexcept
{
    const auto quantize = [](const float value,
                             const float baseline,
                             const float stride,
                             const std::size_t defaultValue,
                             const std::size_t minimum,
                             const std::size_t maximum) {
        const auto steps = static_cast<std::ptrdiff_t>(std::lround(
            (value - baseline) / stride));
        const auto result = static_cast<std::ptrdiff_t>(defaultValue) + steps;
        return static_cast<std::size_t>(std::clamp(
            result,
            static_cast<std::ptrdiff_t>(minimum),
            static_cast<std::ptrdiff_t>(maximum)));
    };
    const auto baseline = calculateLauncherWindowSizeDip(
        {core::defaultLauncherGridColumns, core::defaultLauncherGridRows},
        metrics);
    return {
        quantize(
            clientWidthDip,
            baseline.width,
            metrics.itemWidth + metrics.itemGap,
            core::defaultLauncherGridColumns,
            core::minimumLauncherGridColumns,
            core::maximumLauncherGridColumns),
        quantize(
            clientHeightDip,
            baseline.height,
            metrics.itemHeight + metrics.itemGap,
            core::defaultLauncherGridRows,
            core::minimumLauncherGridRows,
            core::maximumLauncherGridRows),
    };
}

RectPixels quantizeLauncherSizingRectangle(
    const RectPixels& proposed,
    const LauncherResizeEdge edge,
    const std::uint32_t dpi,
    const LauncherMetrics& metrics) noexcept
{
    const auto scale = static_cast<float>(std::max<std::uint32_t>(dpi, 1U)) / 96.0F;
    const auto proposedGrid = calculateLauncherGridSize(
        static_cast<float>(proposed.right - proposed.left) / scale,
        static_cast<float>(proposed.bottom - proposed.top) / scale,
        metrics);
    const auto sizeDip = calculateLauncherWindowSizeDip(proposedGrid, metrics);
    const int width = static_cast<int>(std::lround(sizeDip.width * scale));
    const int height = static_cast<int>(std::lround(sizeDip.height * scale));

    auto result = proposed;
    const bool anchorsRight = edge == LauncherResizeEdge::Left
        || edge == LauncherResizeEdge::TopLeft
        || edge == LauncherResizeEdge::BottomLeft;
    const bool anchorsBottom = edge == LauncherResizeEdge::Top
        || edge == LauncherResizeEdge::TopLeft
        || edge == LauncherResizeEdge::TopRight;
    if (edge != LauncherResizeEdge::Top && edge != LauncherResizeEdge::Bottom) {
        if (anchorsRight) {
            result.left = result.right - width;
        }
        else {
            result.right = result.left + width;
        }
    }
    if (edge != LauncherResizeEdge::Left && edge != LauncherResizeEdge::Right) {
        if (anchorsBottom) {
            result.top = result.bottom - height;
        }
        else {
            result.bottom = result.top + height;
        }
    }
    return result;
}

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
    layout.headerContent = RectDip{
        layout.header.x,
        layout.header.y + metrics.headerVisualOffsetY,
        layout.header.width,
        layout.header.height,
    };
    layout.menuButton = RectDip{
        layout.header.x,
        layout.header.y,
        metrics.headerHeight,
        metrics.headerHeight,
    };
    layout.closeButton = RectDip{
        layout.header.x + std::max(0.0F, layout.header.width - metrics.headerHeight),
        layout.header.y,
        std::min(layout.header.width, metrics.headerHeight),
        metrics.headerHeight,
    };
    layout.pinButton = RectDip{
        std::max(
            layout.header.x,
            layout.closeButton.x - metrics.chromeButtonGap - metrics.headerHeight),
        layout.header.y,
        metrics.headerHeight,
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
    layout.tabItemWidth = metrics.tabWidth;
    layout.tabMinimumWidth = metrics.tabMinimumWidth;
    layout.grid = RectDip{
        metrics.outerPadding,
        y,
        contentWidth,
        std::max(0.0F, tabsY - y - metrics.gridTabGap),
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

std::size_t calculateLauncherGridCapacity(
    const float clientWidthDip,
    const float clientHeightDip,
    const LauncherMetrics& metrics)
{
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = clientWidthDip,
        .clientHeightDip = clientHeightDip,
        .itemCount = 0,
    }, metrics);
    if (metrics.itemWidth <= 0.0F || metrics.itemHeight <= 0.0F
        || layout.grid.height < metrics.itemHeight) {
        return 0;
    }
    const float rowStride = metrics.itemHeight + metrics.itemGap;
    const auto rows = rowStride > 0.0F
        ? 1U + static_cast<std::size_t>(std::floor(
            (layout.grid.height - metrics.itemHeight) / rowStride))
        : 1U;
    return layout.columns * rows;
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
    const RectDip field{
        .x = metrics.horizontalPadding,
        .y = metrics.verticalPadding,
        .width = availableFieldWidth,
        .height = metrics.fieldHeight,
    };
    return SearchPopupLayout{
        .xOffsetDip = 0.0F,
        .gapDip = metrics.gap,
        .windowWidthDip = windowWidth,
        .windowHeightDip = metrics.fieldHeight + (2.0F * metrics.verticalPadding),
        .field = field,
        .edit = RectDip{
            .x = field.x + 38.0F,
            .y = field.y + 8.0F,
            .width = std::max(1.0F, field.width - 50.0F),
            .height = std::max(1.0F, field.height - 16.0F),
        },
    };
}

bool isLauncherDragRegion(
    const LauncherLayout& layout,
    const float xDip,
    const float yDip,
    const bool occupiedItem) noexcept
{
    const bool insideItem = hitTestLauncherItem(layout, xDip, yDip).has_value();
    return !contains(layout.closeButton, xDip, yDip)
        && !contains(layout.menuButton, xDip, yDip)
        && !contains(layout.pinButton, xDip, yDip)
        && !contains(layout.tabs, xDip, yDip)
        && !(insideItem && occupiedItem);
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
    const float yDip,
    const std::size_t firstVisibleIndex) noexcept
{
    if (tabCount == 0 || !contains(layout.tabs, xDip, yDip)) {
        return std::nullopt;
    }
    const auto viewport = calculateLauncherTabViewport(layout, tabCount, firstVisibleIndex);
    if (viewport.visibleCount == 0U || viewport.itemWidth <= 0.0F) {
        return std::nullopt;
    }
    const auto visibleIndex = std::min(
        static_cast<std::size_t>((xDip - layout.tabs.x) / viewport.itemWidth),
        viewport.visibleCount - 1U);
    return viewport.firstIndex + visibleIndex;
}

LauncherTabViewport calculateLauncherTabViewport(
    const LauncherLayout& layout,
    const std::size_t tabCount,
    const std::size_t firstVisibleIndex) noexcept
{
    if (tabCount == 0U || layout.tabs.width <= 0.0F) {
        return {};
    }
    const float minimumWidth = std::max(1.0F, layout.tabMinimumWidth);
    const float requestedWidth = layout.tabItemWidth > 0.0F
        ? std::max(minimumWidth, layout.tabItemWidth)
        : minimumWidth;
    const auto capacity = std::max<std::size_t>(
        1U,
        static_cast<std::size_t>(std::floor(layout.tabs.width / requestedWidth)));
    const auto visibleCount = std::min(tabCount, capacity);
    const auto maximumFirst = tabCount - visibleCount;
    const auto first = std::min(firstVisibleIndex, maximumFirst);
    return {
        .firstIndex = first,
        .visibleCount = visibleCount,
        .itemWidth = layout.tabs.width / static_cast<float>(visibleCount),
    };
}

RectDip calculateLauncherTabRect(
    const LauncherLayout& layout,
    const std::size_t tabCount,
    const std::size_t index,
    const std::size_t firstVisibleIndex) noexcept
{
    const auto viewport = calculateLauncherTabViewport(layout, tabCount, firstVisibleIndex);
    if (!viewport.contains(index)) {
        return {layout.tabs.x, layout.tabs.y, 0.0F, layout.tabs.height};
    }
    return {
        layout.tabs.x
            + viewport.itemWidth * static_cast<float>(index - viewport.firstIndex),
        layout.tabs.y,
        viewport.itemWidth,
        layout.tabs.height,
    };
}

std::optional<TabInsertionTarget> calculateTabInsertionTarget(
    const LauncherLayout& layout,
    const std::size_t tabCount,
    const std::size_t sourceIndex,
    const float xDip,
    const float yDip,
    const std::size_t firstVisibleIndex) noexcept
{
    const auto hovered = hitTestLauncherTab(
        layout, tabCount, xDip, yDip, firstVisibleIndex);
    if (!hovered || sourceIndex >= tabCount) {
        return std::nullopt;
    }
    const auto viewport = calculateLauncherTabViewport(layout, tabCount, firstVisibleIndex);
    const float tabWidth = viewport.itemWidth;
    if (tabWidth <= 0.0F) {
        return std::nullopt;
    }
    const float tabLeft = layout.tabs.x
        + static_cast<float>(*hovered - viewport.firstIndex) * tabWidth;
    const float center = tabLeft + tabWidth / 2.0F;
    constexpr float centerToleranceDip = 1.0F;
    std::size_t insertionIndex{};
    if (xDip < center - centerToleranceDip) {
        insertionIndex = *hovered;
    }
    else if (xDip > center + centerToleranceDip) {
        insertionIndex = *hovered + 1U;
    }
    else {
        insertionIndex = sourceIndex < *hovered ? *hovered + 1U : *hovered;
    }
    insertionIndex = std::min(insertionIndex, tabCount);
    const auto targetIndex = std::min(
        insertionIndex > sourceIndex ? insertionIndex - 1U : insertionIndex,
        tabCount - 1U);
    return TabInsertionTarget{
        .insertionIndex = insertionIndex,
        .targetIndex = targetIndex,
        .xDip = insertionIndex <= viewport.firstIndex
            ? layout.tabs.x
            : insertionIndex >= viewport.firstIndex + viewport.visibleCount
                ? layout.tabs.x + layout.tabs.width
                : layout.tabs.x
                    + static_cast<float>(insertionIndex - viewport.firstIndex) * tabWidth,
    };
}

RectDip calculateTabDragPreview(
    const LauncherLayout& layout,
    const std::size_t tabCount,
    const float cursorXDip,
    const float cursorYDip,
    const std::size_t firstVisibleIndex) noexcept
{
    const float availableWidth = std::max(0.0F, layout.tabs.width);
    const auto viewport = calculateLauncherTabViewport(layout, tabCount, firstVisibleIndex);
    const float tabWidth = viewport.visibleCount > 0U ? viewport.itemWidth : availableWidth;
    const float previewWidth = std::min(
        availableWidth,
        std::clamp(tabWidth, 72.0F, 160.0F));
    const float previewLeft = cursorXDip - previewWidth / 2.0F;
    const float previewHeight = layout.tabs.height;
    const float previewTop = cursorYDip - previewHeight / 2.0F;
    return {
        .x = previewLeft,
        .y = previewTop,
        .width = previewWidth,
        .height = previewHeight,
    };
}

RectDip calculateItemDragPreview(
    const LauncherLayout& layout,
    const float cursorXDip,
    const float cursorYDip) noexcept
{
    const LauncherMetrics metrics{};
    const float previewWidth = layout.items.empty()
        ? metrics.itemWidth
        : layout.items.front().width;
    const float previewHeight = layout.items.empty()
        ? metrics.itemHeight
        : layout.items.front().height;
    return {
        .x = cursorXDip - previewWidth / 2.0F,
        .y = cursorYDip - previewHeight / 2.0F,
        .width = previewWidth,
        .height = previewHeight,
    };
}

LauncherHoverTarget hitTestLauncherHover(
    const LauncherLayout& layout,
    const std::size_t tabCount,
    const std::size_t visibleItemCount,
    const bool filtering,
    const float xDip,
    const float yDip,
    const std::size_t firstVisibleIndex) noexcept
{
    if (contains(layout.menuButton, xDip, yDip)) {
        return {LauncherHoverRegion::Menu, 0};
    }
    if (contains(layout.pinButton, xDip, yDip)) {
        return {LauncherHoverRegion::Pin, 0};
    }
    if (contains(layout.closeButton, xDip, yDip)) {
        return {LauncherHoverRegion::Close, 0};
    }
    if (!filtering) {
        if (const auto tab = hitTestLauncherTab(
                layout, tabCount, xDip, yDip, firstVisibleIndex)) {
            return {LauncherHoverRegion::Tab, *tab};
        }
    }
    if (const auto item = hitTestLauncherItem(layout, xDip, yDip);
        item && *item < visibleItemCount) {
        return {LauncherHoverRegion::Item, *item};
    }
    return {};
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

RectPixels calculateCursorCenteredWindowRectangle(
    const RectPixels& workArea,
    const int cursorX,
    const int cursorY,
    const int windowWidth,
    const int windowHeight) noexcept
{
    const int availableWidth = std::max(0, workArea.right - workArea.left);
    const int availableHeight = std::max(0, workArea.bottom - workArea.top);
    const int constrainedWidth = std::clamp(windowWidth, 0, availableWidth);
    const int constrainedHeight = std::clamp(windowHeight, 0, availableHeight);
    const int left = std::clamp(
        cursorX - constrainedWidth / 2,
        workArea.left,
        workArea.right - constrainedWidth);
    const int top = std::clamp(
        cursorY - constrainedHeight / 2,
        workArea.top,
        workArea.bottom - constrainedHeight);
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
