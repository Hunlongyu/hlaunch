#pragma once

#include "activation/activation_context.h"
#include "core/data_model.h"
#include "core/item_operations.h"
#include "core/search_index.h"
#include "platform/windows/drop_item_resolver.h"
#include "platform/windows/drop_target.h"
#include "platform/windows/icon_loader.h"
#include "platform/windows/window_effects.h"
#include "ui/item_editor_dialog.h"
#include "ui/search_window.h"
#include "ui/theme.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <winrt/base.h>

#include <functional>
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
        core::ThemeMode themeMode = core::ThemeMode::Dark);
    void setThemeMode(core::ThemeMode themeMode);
    void setWindowEffects(const platform::windows::WindowEffects& effects);
    void refreshSystemAppearance();
    void setDocumentChangedHandler(DocumentChangedHandler handler);
    void setDeleteConfirmationHandler(DeleteConfirmationHandler handler);
    void setItemEditorHandler(ItemEditorHandler handler);
    void setSettingsHandler(SettingsHandler handler);
    void show();
    void showAtScreenEdge(const activation::ScreenEdgeHit& hit);
    void hide();
    void toggle();
    void close();

    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool createDeviceIndependentResources();
    [[nodiscard]] bool createTextFormats();
    [[nodiscard]] bool createDeviceResources();
    void discardDeviceResources() noexcept;
    void positionOnCursorMonitor();
    void positionOnScreenEdge(const activation::ScreenEdgeHit& hit);
    void positionSearchWindow();
    void render();
    [[nodiscard]] bool handleKeyDown(WPARAM key);
    [[nodiscard]] bool handleSearchKeyDown(WPARAM key);
    void beginSearch(std::wstring_view initialText = {});
    void updateSearch(std::wstring_view query);
    void handleMouseWheel(short delta);
    void showAddEditor();
    void showEditEditor(std::size_t absoluteIndex);
    void showItemContextMenu(std::size_t absoluteIndex, POINT screenPoint);
    void showLauncherContextMenu(POINT screenPoint);
    void showEmptySlotContextMenu(POINT screenPoint);
    void showTabContextMenu(std::size_t tabIndex, POINT screenPoint);
    void addPage();
    void deletePage(std::size_t tabIndex);
    void renamePage(std::size_t tabIndex);
    void toggleWindowLock();
    void scheduleAutoHide();
    void deleteItem(std::size_t absoluteIndex);
    void beginItemDrag(std::size_t absoluteIndex, POINT clientPoint);
    void updateItemDrag(POINT clientPoint);
    void finishItemDrag(POINT clientPoint);
    void cancelItemDrag() noexcept;
    void submitDroppedSources(
        std::vector<platform::windows::DroppedSource> sources,
        POINTL screenPoint);
    void applyDropImport(platform::windows::DropImportResult result);
    void shutdownDropServices() noexcept;
    void applyIconLoadResult(platform::windows::IconLoadResult result);
    void shutdownIconServices() noexcept;
    void rebuildSearchIndex();
    void activateFocusedItem();
    void changeActiveTab(std::size_t tabIndex);
    [[nodiscard]] const core::Tab* activeTab() const noexcept;
    struct DisplayedItem {
        const core::LaunchItem* item{};
        const core::Tab* tab{};
    };
    struct InternalDropTarget {
        std::size_t tabIndex{};
        std::size_t itemIndex{};
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
    [[nodiscard]] std::optional<core::ItemLocation>
    itemLocationForDisplayedIndex(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<DisplayedItem> displayedItem(std::size_t index) const noexcept;
    [[nodiscard]] bool isSearchFiltering() const noexcept;
    [[nodiscard]] std::size_t totalItemCount() const noexcept;
    [[nodiscard]] std::size_t pageCapacity() const;
    [[nodiscard]] std::size_t maximumPageOffset() const;
    void ensureFocusedItemVisible();
    [[nodiscard]] std::size_t visibleItemCount() const;
    [[nodiscard]] std::size_t displayedTileCount() const;
    [[nodiscard]] ID2D1Bitmap* itemIconBitmap(const core::LaunchItem& item);
    void drawText(
        std::wstring_view text,
        const D2D1_RECT_F& bounds,
        IDWriteTextFormat* format,
        ID2D1Brush* brush);

    HWND window_{};
    UINT dpi_{96};
    bool translucentSurface_{true};
    platform::windows::WindowEffects windowEffects_{};
    core::ThemeMode themeMode_{core::ThemeMode::Dark};
    bool searchVisible_{};
    core::ItemsDocument document_{};
    core::SearchIndex searchIndex_{};
    std::vector<core::SearchResult> searchResults_{};
    std::size_t activeTabIndex_{};
    std::size_t focusedItemIndex_{};
    bool keyboardSelectionActive_{};
    std::size_t pageOffset_{};
    int wheelDeltaRemainder_{};
    bool windowFocused_{};
    bool windowLocked_{};
    bool hasPositioned_{};
    std::optional<core::ItemLocation> itemDragSource_{};
    std::optional<InternalDropTarget> itemDropTarget_{};
    std::optional<std::size_t> pressedItemIndex_{};
    POINT itemDragStart_{};
    bool itemDragActive_{};
    LaunchHandler launchHandler_{};
    DocumentChangedHandler documentChangedHandler_{};
    DeleteConfirmationHandler deleteConfirmationHandler_{};
    ItemEditorHandler itemEditorHandler_{};
    SettingsHandler settingsHandler_{};
    platform::windows::DropTarget dropTarget_{};
    std::unique_ptr<platform::windows::DropItemResolver> dropResolver_{};
    std::unique_ptr<platform::windows::IconLoader> iconLoader_{};
    std::unordered_map<std::string, CachedItemIcon> iconCache_{};
    std::uint64_t iconCacheUseSequence_{};
    SearchWindow searchWindow_{};
    winrt::com_ptr<ID2D1Factory> d2dFactory_{};
    winrt::com_ptr<IDWriteFactory> writeFactory_{};
    winrt::com_ptr<ID2D1HwndRenderTarget> renderTarget_{};
    winrt::com_ptr<IDWriteTextFormat> titleFormat_{};
    winrt::com_ptr<IDWriteTextFormat> bodyFormat_{};
    winrt::com_ptr<IDWriteTextFormat> smallFormat_{};
    winrt::com_ptr<IDWriteTextFormat> captionFormat_{};
    winrt::com_ptr<IDWriteTextFormat> tabFormat_{};
    winrt::com_ptr<IDWriteTextFormat> iconFormat_{};
    winrt::com_ptr<ID2D1SolidColorBrush> backgroundBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> surfaceBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> elevatedBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> accentBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> textBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> mutedTextBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> borderBrush_{};
};

} // namespace hlaunch::ui
