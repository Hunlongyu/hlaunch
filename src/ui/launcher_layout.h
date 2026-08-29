#pragma once

#include "core/data_model.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hlaunch::ui {

inline constexpr float defaultLauncherTabMinimumWidthDip = 64.0F;

enum class TabIndicatorStyle {
    Underline,
    Topline,
    Border,
    Pill,
    None,
};

struct RectDip {
    float x{};
    float y{};
    float width{};
    float height{};

    bool operator==(const RectDip&) const = default;
};

[[nodiscard]] RectDip insetRectForInsideStroke(
    const RectDip& bounds,
    float strokeWidth) noexcept;

struct LauncherMetrics {
    float outerPadding{8.0F};
    float headerHeight{16.0F};
    float sectionGap{4.0F};
    float gridTabGap{8.0F};
    float tabHeight{30.0F};
    float itemWidth{72.0F};
    float itemHeight{62.0F};
    float itemGap{4.0F};
    float itemCornerRadius{2.0F};
    float itemIconSize{32.0F};
    float itemLabelFontSize{12.0F};
    float itemBorderWidth{1.0F};
    float itemHoverBorderWidth{1.0F};
    float itemFocusBorderWidth{1.0F};
    float itemDropBorderWidth{3.0F};
    bool showItemText{true};
    float titleFontSize{13.0F};
    float chromeIconSize{14.0F};
    float pinIconSize{12.0F};
    float headerVisualOffsetY{-1.0F};
    float chromeCornerRadius{4.0F};
    float chromeButtonGap{4.0F};
    float tabCornerRadius{};
    float tabWidth{};
    float tabMinimumWidth{defaultLauncherTabMinimumWidthDip};
    float tabHorizontalInset{};
    float tabFontSize{13.0F};
    std::uint16_t tabFontWeight{500};
    float tabUnderlineThickness{4.0F};
    float tabIndicatorWidth{};
    float tabIndicatorWidthPercent{100.0F};
    float tabIndicatorOffset{};
    float tabIndicatorCornerRadius{};
    TabIndicatorStyle tabIndicatorStyle{TabIndicatorStyle::Underline};
};

[[nodiscard]] std::uint32_t iconPixelSizeForDpi(
    float iconSizeDip,
    std::uint32_t dpi) noexcept;

struct LauncherLayout {
    RectDip header{};
    RectDip headerContent{};
    RectDip menuButton{};
    RectDip pinButton{};
    RectDip closeButton{};
    RectDip tabs{};
    RectDip grid{};
    RectDip gridContent{};
    float tabItemWidth{};
    float tabMinimumWidth{defaultLauncherTabMinimumWidthDip};
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
    float verticalPadding{6.0F};
    float fieldHeight{36.0F};
    float gap{6.0F};
};

struct SearchPopupLayout {
    float xOffsetDip{};
    float gapDip{};
    float windowWidthDip{};
    float windowHeightDip{};
    RectDip field{};
    RectDip edit{};
};

struct RectPixels {
    int left{};
    int top{};
    int right{};
    int bottom{};

    bool operator==(const RectPixels&) const = default;
};

struct SizeDip {
    float width{};
    float height{};

    bool operator==(const SizeDip&) const = default;
};

struct LauncherGridSize {
    std::size_t columns{core::defaultLauncherGridColumns};
    std::size_t rows{core::defaultLauncherGridRows};

    bool operator==(const LauncherGridSize&) const = default;
};

enum class LauncherResizeEdge : std::uint8_t {
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
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

enum class LauncherHoverRegion : std::uint8_t {
    None,
    Menu,
    Pin,
    Close,
    Tab,
    Item,
};

struct LauncherHoverTarget final {
    LauncherHoverRegion region{LauncherHoverRegion::None};
    std::size_t index{};

    bool operator==(const LauncherHoverTarget&) const = default;
};

struct TabInsertionTarget final {
    std::size_t insertionIndex{};
    std::size_t targetIndex{};
    float xDip{};

    bool operator==(const TabInsertionTarget&) const = default;
};

struct LauncherTabViewport final {
    std::size_t firstIndex{};
    std::size_t visibleCount{};
    float itemWidth{};

    [[nodiscard]] bool contains(std::size_t index) const noexcept
    {
        return index >= firstIndex && index < firstIndex + visibleCount;
    }

    bool operator==(const LauncherTabViewport&) const = default;
};

[[nodiscard]] LauncherLayout calculateLauncherLayout(
    const LauncherLayoutRequest& request,
    const LauncherMetrics& metrics = {});

[[nodiscard]] std::size_t calculateLauncherGridCapacity(
    float clientWidthDip,
    float clientHeightDip,
    const LauncherMetrics& metrics = {});

[[nodiscard]] SizeDip calculateLauncherWindowSizeDip(
    LauncherGridSize gridSize,
    const LauncherMetrics& metrics = {}) noexcept;

[[nodiscard]] LauncherGridSize calculateLauncherGridSize(
    float clientWidthDip,
    float clientHeightDip,
    const LauncherMetrics& metrics = {}) noexcept;

[[nodiscard]] RectPixels quantizeLauncherSizingRectangle(
    const RectPixels& proposed,
    LauncherResizeEdge edge,
    std::uint32_t dpi,
    const LauncherMetrics& metrics = {}) noexcept;

[[nodiscard]] SearchPopupLayout calculateSearchPopupLayout(
    const LauncherLayout& launcherLayout,
    const SearchPopupMetrics& metrics = {});

[[nodiscard]] bool isLauncherDragRegion(
    const LauncherLayout& layout,
    float xDip,
    float yDip,
    bool occupiedItem = false) noexcept;

[[nodiscard]] std::optional<std::size_t> hitTestLauncherItem(
    const LauncherLayout& layout,
    float xDip,
    float yDip) noexcept;

[[nodiscard]] std::optional<std::size_t> hitTestLauncherTab(
    const LauncherLayout& layout,
    std::size_t tabCount,
    float xDip,
    float yDip,
    std::size_t firstVisibleIndex = 0U) noexcept;

[[nodiscard]] LauncherTabViewport calculateLauncherTabViewport(
    const LauncherLayout& layout,
    std::size_t tabCount,
    std::size_t firstVisibleIndex = 0U) noexcept;

[[nodiscard]] RectDip calculateLauncherTabRect(
    const LauncherLayout& layout,
    std::size_t tabCount,
    std::size_t index,
    std::size_t firstVisibleIndex = 0U) noexcept;

[[nodiscard]] std::optional<TabInsertionTarget> calculateTabInsertionTarget(
    const LauncherLayout& layout,
    std::size_t tabCount,
    std::size_t sourceIndex,
    float xDip,
    float yDip,
    std::size_t firstVisibleIndex = 0U) noexcept;

[[nodiscard]] RectDip calculateTabDragPreview(
    const LauncherLayout& layout,
    std::size_t tabCount,
    float cursorXDip,
    float cursorYDip,
    std::size_t firstVisibleIndex = 0U) noexcept;

[[nodiscard]] RectDip calculateItemDragPreview(
    const LauncherLayout& layout,
    float cursorXDip,
    float cursorYDip) noexcept;

[[nodiscard]] LauncherHoverTarget hitTestLauncherHover(
    const LauncherLayout& layout,
    std::size_t tabCount,
    std::size_t visibleItemCount,
    bool filtering,
    float xDip,
    float yDip,
    std::size_t firstVisibleIndex = 0U) noexcept;

[[nodiscard]] std::optional<std::size_t> navigateGridItem(
    std::optional<std::size_t> currentIndex,
    std::size_t itemCount,
    std::size_t columns,
    GridNavigationDirection direction) noexcept;

[[nodiscard]] std::optional<std::size_t> cycleLauncherTab(
    std::size_t currentIndex,
    std::size_t tabCount,
    bool backward) noexcept;

[[nodiscard]] RectPixels calculateCursorCenteredWindowRectangle(
    const RectPixels& workArea,
    int cursorX,
    int cursorY,
    int windowWidth,
    int windowHeight) noexcept;

[[nodiscard]] RectPixels calculateScreenEdgeWindowRectangle(
    const ScreenEdgePlacementRequest& request) noexcept;

} // namespace hlaunch::ui
