#pragma once

#include "activation/activation_context.h"
#include "core/data_model.h"
#include "core/item_operations.h"
#include "core/search_index.h"
#include "platform/windows/drop_item_resolver.h"
#include "platform/windows/drop_target.h"
#include "platform/windows/icon_loader.h"
#include "platform/windows/item_path_policy.h"
#include "platform/windows/window_effects.h"
#include "ui/item_editor_dialog.h"
#include "ui/launcher_accessibility.h"
#include "ui/launcher_layout.h"
#include "ui/search_window.h"
#include "ui/visual_style.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <winrt/base.h>

#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hlaunch::ui {

class LauncherWindow final {
public:
    using LaunchHandler = std::function<void(const core::LaunchItem&)>;
    using DocumentChangedHandler = std::function<void(const core::ItemsDocument&)>;
    using GridSizeChangedHandler = std::function<bool(std::uint16_t, std::uint16_t)>;
    using SettingsHandler = std::function<void()>;
    using DeleteConfirmationHandler = std::function<bool(HWND, const core::LaunchItem&)>;
    using ItemEditorHandler = std::function<std::optional<ItemEditorResult>(
        HWND,
        const std::vector<core::Tab>&,
        std::size_t,
        const core::LaunchItem*)>;

    LauncherWindow() = default;
    ~LauncherWindow();

    LauncherWindow(const LauncherWindow&) = delete;
    LauncherWindow& operator=(const LauncherWindow&) = delete;

    [[nodiscard]] bool create(
        HINSTANCE instance,
        const platform::windows::WindowEffects& effects,
        bool showSearch,
        core::ItemsDocument document,
        LaunchHandler launchHandler,
        DocumentChangedHandler documentChangedHandler = {},
        std::filesystem::path iconCacheDirectory = {},
        platform::windows::ItemPathContext itemPathContext = {},
        std::uint16_t gridColumns = core::defaultLauncherGridColumns,
        std::uint16_t gridRows = core::defaultLauncherGridRows,
        LauncherMetrics metrics = {});
    void setWindowEffects(const platform::windows::WindowEffects& effects);
    void refreshSystemAppearance();
    void setDocumentChangedHandler(DocumentChangedHandler handler);
    void setGridSizeChangedHandler(GridSizeChangedHandler handler);
    void setDeleteConfirmationHandler(DeleteConfirmationHandler handler);
    void setItemEditorHandler(ItemEditorHandler handler);
    void setSettingsHandler(SettingsHandler handler);
    void show();
    void showAtScreenEdge(const activation::ScreenEdgeHit& hit);
    void hide();
    void toggle();
    void close();
    void recordSuccessfulLaunch(std::string_view itemId);
    void hideAfterSuccessfulLaunchIfNeeded();

    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK dragPreviewWindowProcedure(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool createDeviceIndependentResources();
    [[nodiscard]] bool createTextFormats();
    [[nodiscard]] bool createDeviceResources();
    [[nodiscard]] bool createDragPreviewResources();
    void discardDeviceResources() noexcept;
    void discardDragPreviewResources() noexcept;
    void updateDragPreviewWindow(const RectDip& clientBounds);
    void hideDragPreviewWindow() noexcept;
    void renderDragPreview();
    void positionOnCursorMonitor();
    void positionOnScreenEdge(const activation::ScreenEdgeHit& hit);
    void positionSearchWindow();
    void createItemTooltip(HINSTANCE instance);
    void showItemTooltip();
    void hideItemTooltip() noexcept;
    [[nodiscard]] bool applyGridSizeChange(LauncherGridSize gridSize);
    [[nodiscard]] SIZE launcherWindowSizePixels() const noexcept;
    void updateHover(POINT clientPoint);
    void clearHover() noexcept;
    void ensureActiveTabVisible() noexcept;
    void render();
    [[nodiscard]] bool handleKeyDown(WPARAM key);
    [[nodiscard]] bool handleSearchKeyDown(WPARAM key);
    void beginSearch(std::wstring_view initialText = {});
    void updateSearch(std::wstring_view query);
    void handleMouseWheel(short delta);
    void showAddEditor(std::optional<std::size_t> targetGridSlot = std::nullopt);
    void showEditEditor(std::size_t absoluteIndex);
    void showItemContextMenu(std::size_t absoluteIndex, POINT screenPoint);
    void showLauncherContextMenu(POINT screenPoint);
    void showEmptySlotContextMenu(std::size_t gridSlot, POINT screenPoint);
    void showTabContextMenu(std::size_t tabIndex, POINT screenPoint);
    void launchAllInPage(std::size_t tabIndex);
    void addPage();
    void deletePage(std::size_t tabIndex);
    void renamePage(std::size_t tabIndex);
    void movePage(std::size_t tabIndex, std::size_t targetTabIndex);
    void toggleWindowPin();
    void scheduleAutoHide();
    void deleteItem(std::size_t absoluteIndex);
    void moveItemToTab(std::size_t absoluteIndex, std::size_t targetTabIndex);
    void beginItemDrag(std::size_t absoluteIndex, POINT clientPoint);
    void updateItemDrag(POINT clientPoint);
    void finishItemDrag(POINT clientPoint);
    void cancelItemDrag() noexcept;
    void beginTabDrag(std::size_t tabIndex, POINT clientPoint);
    void updateTabDrag(POINT clientPoint);
    void finishTabDrag(POINT clientPoint);
    void cancelTabDrag() noexcept;
    void submitDroppedSources(
        std::vector<platform::windows::DroppedSource> sources,
        POINTL screenPoint);
    void applyDropImport(platform::windows::DropImportResult result);
    void shutdownDropServices() noexcept;
    void applyIconLoadResult(platform::windows::IconLoadResult result);
    void shutdownIconServices() noexcept;
    void rebuildSearchIndex();
    [[nodiscard]] std::vector<LauncherAccessibleNode> accessibilitySnapshot() const;
    void invokeAccessibleNode(std::wstring_view key);
    void focusAccessibleNode(std::wstring_view key);
    void activateFocusedItem();
    [[nodiscard]] std::optional<core::LaunchItem> resolveItemForUse(
        const core::LaunchItem& item,
        std::wstring_view action);
    void changeActiveTab(std::size_t tabIndex);
    [[nodiscard]] const core::Tab* activeTab() const noexcept;
    struct DisplayedItem {
        const core::LaunchItem* item{};
    };
    struct InternalDropTarget {
        std::size_t tabIndex{};
        std::size_t gridSlot{};
        bool tabTarget{};
        std::optional<std::size_t> displayedTileIndex{};

        bool operator==(const InternalDropTarget&) const = default;
    };
    struct CachedItemIcon {
        std::string sourceKey{};
        std::uint32_t requestedPixelSize{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint8_t> pixels{};
        winrt::com_ptr<ID2D1Bitmap> bitmap{};
        std::uint64_t lastUsed{};
        bool pending{};
        bool failed{};
    };
    struct IconCacheKey {
        std::string itemId{};
        std::uint32_t pixelSize{};

        bool operator==(const IconCacheKey&) const = default;
    };
    struct IconCacheKeyHash {
        [[nodiscard]] std::size_t operator()(const IconCacheKey& key) const noexcept;
    };
    [[nodiscard]] std::optional<core::ItemLocation>
    itemLocationForDisplayedIndex(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<DisplayedItem> displayedItem(std::size_t index) const noexcept;
    [[nodiscard]] bool isSearchFiltering() const noexcept;
    [[nodiscard]] std::size_t totalItemCount() const noexcept;
    [[nodiscard]] std::optional<std::size_t> nearestDisplayedItemIndex(
        std::size_t preferred,
        bool preferForward = true) const noexcept;
    [[nodiscard]] std::size_t pageCapacity() const;
    [[nodiscard]] std::size_t maximumPageOffset() const;
    void ensureFocusedItemVisible();
    [[nodiscard]] std::size_t visibleItemCount() const;
    [[nodiscard]] std::size_t displayedTileCount() const;
    [[nodiscard]] CachedItemIcon* ensureItemIcon(
        const core::LaunchItem& item,
        std::uint32_t dpi);
    [[nodiscard]] ID2D1Bitmap* itemIconBitmap(const core::LaunchItem& item);
    void drawText(
        std::wstring_view text,
        const D2D1_RECT_F& bounds,
        IDWriteTextFormat* format,
        ID2D1Brush* brush);

    HWND window_{};
    HWND dragPreviewWindow_{};
    HWND itemTooltip_{};
    UINT dpi_{96};
    UINT dragPreviewDpi_{96};
    bool translucentSurface_{true};
    platform::windows::WindowEffects windowEffects_{};
    bool searchVisible_{};
    core::ItemsDocument document_{};
    core::SearchIndex searchIndex_{};
    std::vector<core::SearchResult> searchResults_{};
    std::size_t activeTabIndex_{};
    std::size_t firstVisibleTabIndex_{};
    std::size_t focusedItemIndex_{};
    bool keyboardSelectionActive_{};
    std::size_t pageOffset_{};
    int wheelDeltaRemainder_{};
    bool windowFocused_{};
    std::wstring accessibilityFocusKey_{L"root"};
    bool windowPinned_{};
    std::uint16_t gridColumns_{core::defaultLauncherGridColumns};
    std::uint16_t gridRows_{core::defaultLauncherGridRows};
    LauncherMetrics metrics_{};
    std::wstring effectiveFontFamily_{};
    std::optional<LauncherGridSize> pendingGridSize_{};
    std::optional<RECT> resizeStartRectangle_{};
    std::optional<core::ItemsDocument> resizeStartDocument_{};
    std::optional<std::size_t> resizePreviewColumns_{};
    std::optional<POINT> emptySlotWindowDragStart_{};
    std::optional<core::ItemLocation> itemDragSource_{};
    std::optional<InternalDropTarget> itemDropTarget_{};
    std::optional<std::size_t> pressedItemIndex_{};
    POINT itemDragStart_{};
    POINT itemDragCurrentPoint_{};
    bool itemDragActive_{};
    std::optional<std::size_t> tabDragSourceIndex_{};
    std::optional<std::size_t> tabDragTargetIndex_{};
    std::optional<std::size_t> tabDragInsertionIndex_{};
    std::optional<float> tabDragInsertionXDip_{};
    POINT tabDragStart_{};
    POINT tabDragCurrentPoint_{};
    bool tabDragActive_{};
    LauncherHoverTarget hoverTarget_{};
    std::wstring hoverItemName_{};
    POINT itemTooltipPosition_{};
    LaunchHandler launchHandler_{};
    DocumentChangedHandler documentChangedHandler_{};
    GridSizeChangedHandler gridSizeChangedHandler_{};
    DeleteConfirmationHandler deleteConfirmationHandler_{};
    ItemEditorHandler itemEditorHandler_{};
    SettingsHandler settingsHandler_{};
    platform::windows::DropTarget dropTarget_{};
    platform::windows::ItemPathContext itemPathContext_{};
    std::unique_ptr<platform::windows::DropItemResolver> dropResolver_{};
    std::unique_ptr<platform::windows::IconLoader> iconLoader_{};
    std::unordered_map<IconCacheKey, CachedItemIcon, IconCacheKeyHash> iconCache_{};
    std::uint64_t iconCacheUseSequence_{};
    SearchWindow searchWindow_{};
    std::unique_ptr<LauncherAccessibility> accessibility_{};
    winrt::com_ptr<ID2D1Factory> d2dFactory_{};
    winrt::com_ptr<IDWriteFactory> writeFactory_{};
    winrt::com_ptr<ID2D1HwndRenderTarget> renderTarget_{};
    winrt::com_ptr<ID2D1HwndRenderTarget> dragPreviewRenderTarget_{};
    winrt::com_ptr<IDWriteTextFormat> titleFormat_{};
    winrt::com_ptr<IDWriteTextFormat> bodyFormat_{};
    winrt::com_ptr<IDWriteTextFormat> smallFormat_{};
    winrt::com_ptr<IDWriteTextFormat> tabFormat_{};
    winrt::com_ptr<IDWriteTextFormat> iconFormat_{};
    winrt::com_ptr<IDWriteTextFormat> chromeIconFormat_{};
    winrt::com_ptr<IDWriteTextFormat> pinIconFormat_{};
    winrt::com_ptr<ID2D1SolidColorBrush> backgroundBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> surfaceBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> elevatedBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> accentBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> tabBackgroundBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> tabHoverBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> tabIndicatorBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> dangerBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> itemHighlightBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> textBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> mutedTextBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> borderBrush_{};
};

} // namespace hlaunch::ui
