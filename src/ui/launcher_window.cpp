#include "ui/launcher_window.h"

#include "core/item_operations.h"
#include "core/data_validation.h"
#include "platform/windows/shell_launcher.h"
#include "platform/windows/search_text.h"
#include "platform/windows/uuid.h"
#include "ui/clipboard.h"
#include "ui/item_context_menu.h"
#include "ui/item_editor_dialog.h"
#include "ui/launcher_context_menu.h"
#include "ui/launcher_layout.h"
#include "ui/text_prompt_dialog.h"
#include "ui/visual_style.h"
#include "ui/system_appearance.h"
#include "ui/task_dialog.h"
#include "resource.h"

#include <CommCtrl.h>
#include <d2d1helper.h>
#include <windowsx.h>
#include <wil/resource.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr wchar_t launcherWindowClass[] = L"HLaunch.LauncherWindow.v1";
constexpr wchar_t dragPreviewWindowClass[] = L"HLaunch.DragPreviewWindow.v1";
constexpr std::size_t defaultVisibleItems = 40;
constexpr std::size_t maximumIconCacheEntries = 128;
constexpr UINT_PTR autoHideTimer = 1;
constexpr UINT_PTR itemTooltipTimer = 2;
constexpr UINT itemTooltipDelayMs = 150;
constexpr UINT dropImportCompletedMessage = WM_APP + 0x43U;
constexpr UINT iconLoadCompletedMessage = WM_APP + 0x44U;
constexpr std::wstring_view accessibleRootKey = L"root";
constexpr std::wstring_view accessibleMenuKey = L"chrome:menu";
constexpr std::wstring_view accessiblePinKey = L"chrome:pin";
constexpr std::wstring_view accessibleCloseKey = L"chrome:close";
constexpr std::wstring_view accessibleTabsKey = L"tabs";
constexpr std::wstring_view accessibleItemsKey = L"items";

std::string currentUtcTimestamp() noexcept
{
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    std::array<char, 32> value{};
    const auto length = std::snprintf(
        value.data(),
        value.size(),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        utc.wYear,
        utc.wMonth,
        utc.wDay,
        utc.wHour,
        utc.wMinute,
        utc.wSecond,
        utc.wMilliseconds);
    return length > 0
        ? std::string{value.data(), static_cast<std::size_t>(length)}
        : std::string{};
}

platform::windows::WindowEffects effectiveWindowEffects(
    const platform::windows::WindowEffects& configured) noexcept
{
    if (!isHighContrastEnabled()) {
        return configured;
    }
    return {
        .backdrop = platform::windows::WindowBackdrop::Solid,
        .opacityPercent = 100,
    };
}

std::wstring utf8ToWide(const std::string_view value)
{
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return L"?";
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required) != required) {
        return L"?";
    }
    return result;
}

std::optional<std::string> wideToUtf8(const std::wstring_view value)
{
    if (value.empty()) {
        return std::string{};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return std::nullopt;
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), required, nullptr, nullptr) != required) {
        return std::nullopt;
    }
    return result;
}

std::wstring itemGlyph(const std::wstring_view name)
{
    if (name.empty()) {
        return L"?";
    }
    const std::size_t length = IS_HIGH_SURROGATE(name.front()) && name.size() > 1
        && IS_LOW_SURROGATE(name[1])
        ? 2U
        : 1U;
    return std::wstring{name.substr(0, length)};
}

std::uint32_t itemColor(const core::ItemType type) noexcept
{
    switch (type) {
    case core::ItemType::Application:
        return 0x3B82F6;
    case core::ItemType::File:
        return 0x8B5CF6;
    case core::ItemType::Folder:
        return 0xF59E0B;
    case core::ItemType::Url:
        return 0x06B6D4;
    case core::ItemType::Shortcut:
        return 0x14B8A6;
    }
    return 0x64748B;
}

std::string iconSourceKey(const core::LaunchItem& item)
{
    std::string result{};
    result.reserve(item.target.size() + (item.icon ? item.icon->size() : 0U) + 1U);
    if (item.icon) {
        result.append(*item.icon);
    }
    result.push_back('\0');
    result.append(item.target);
    return result;
}

D2D1_RECT_F toD2dRect(const RectDip& rectangle)
{
    return D2D1::RectF(
        rectangle.x,
        rectangle.y,
        rectangle.x + rectangle.width,
        rectangle.y + rectangle.height);
}

D2D1_RECT_F pixelAlignedIconRect(
    const float leftDip,
    const float topDip,
    const float sizeDip,
    const std::uint32_t dpi) noexcept
{
    const auto effectiveDpi = std::max<std::uint32_t>(dpi, 1U);
    const auto scale = static_cast<float>(effectiveDpi) / 96.0F;
    const auto left = std::round(leftDip * scale) / scale;
    const auto top = std::round(topDip * scale) / scale;
    const auto physicalSize = iconPixelSizeForDpi(sizeDip, effectiveDpi);
    const auto alignedSize = static_cast<float>(physicalSize) / scale;
    return D2D1::RectF(left, top, left + alignedSize, top + alignedSize);
}

bool systemFontFamilyAvailable(
    IDWriteFactory* const writeFactory,
    const wchar_t* const fontFamily) noexcept
{
    if (writeFactory == nullptr || fontFamily == nullptr) {
        return false;
    }

    winrt::com_ptr<IDWriteFontCollection> fontCollection{};
    if (FAILED(writeFactory->GetSystemFontCollection(fontCollection.put()))) {
        return false;
    }

    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(fontCollection->FindFamilyName(fontFamily, &familyIndex, &exists))
        && exists != FALSE;
}

} // namespace

std::size_t LauncherWindow::IconCacheKeyHash::operator()(
    const IconCacheKey& key) const noexcept
{
    const auto itemHash = std::hash<std::string>{}(key.itemId);
    const auto sizeHash = std::hash<std::uint32_t>{}(key.pixelSize);
    return itemHash ^ (sizeHash + 0x9e3779b9U + (itemHash << 6U) + (itemHash >> 2U));
}

LauncherWindow::~LauncherWindow()
{
    shutdownIconServices();
    shutdownDropServices();
    if (itemTooltip_) {
        DestroyWindow(itemTooltip_);
    }
    if (dragPreviewWindow_) {
        DestroyWindow(dragPreviewWindow_);
    }
    if (window_) {
        DestroyWindow(window_);
    }
}

bool LauncherWindow::create(
    const HINSTANCE instance,
    const platform::windows::WindowEffects& effects,
    const bool showSearch,
    core::ItemsDocument document,
    LaunchHandler launchHandler,
    DocumentChangedHandler documentChangedHandler,
    std::filesystem::path iconCacheDirectory,
    platform::windows::ItemPathContext itemPathContext,
    const std::uint16_t gridColumns,
    const std::uint16_t gridRows,
    LauncherMetrics metrics)
{
    document_ = std::move(document);
    core::normalizeGridSlots(document_);
    rebuildSearchIndex();
    launchHandler_ = std::move(launchHandler);
    documentChangedHandler_ = std::move(documentChangedHandler);
    windowEffects_ = effects;
    itemPathContext_ = std::move(itemPathContext);
    gridColumns_ = std::clamp(
        gridColumns,
        core::minimumLauncherGridColumns,
        core::maximumLauncherGridColumns);
    gridRows_ = std::clamp(
        gridRows,
        core::minimumLauncherGridRows,
        core::maximumLauncherGridRows);
    metrics_ = std::move(metrics);
    if (itemPathContext_.executableDirectory.empty()) {
        std::wstring modulePath(32'768, L'\0');
        const auto length = GetModuleFileNameW(
            nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        if (length == 0 || length >= modulePath.size()) {
            return false;
        }
        modulePath.resize(length);
        itemPathContext_.executableDirectory = std::filesystem::path{modulePath}.parent_path();
    }
    activeTabIndex_ = 0;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW | CS_DBLCLKS;
    windowClass.lpfnWndProc = &LauncherWindow::windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_HLAUNCH));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_HLAUNCH),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    windowClass.lpszClassName = launcherWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    WNDCLASSEXW dragPreviewClass{};
    dragPreviewClass.cbSize = sizeof(WNDCLASSEXW);
    dragPreviewClass.style = CS_DROPSHADOW;
    dragPreviewClass.lpfnWndProc = &LauncherWindow::dragPreviewWindowProcedure;
    dragPreviewClass.hInstance = instance;
    dragPreviewClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dragPreviewClass.lpszClassName = dragPreviewWindowClass;
    if (!RegisterClassExW(&dragPreviewClass)
        && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    dpi_ = GetDpiForSystem();
    const auto initialSize = launcherWindowSizePixels();
    const auto width = initialSize.cx;
    const auto height = initialSize.cy;
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const auto x = workArea.left + ((workArea.right - workArea.left - width) / 2);
    const auto y = workArea.top + ((workArea.bottom - workArea.top - height) / 2);
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        launcherWindowClass,
        L"HLaunch",
        WS_POPUP,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        this);
    if (!window_) {
        return false;
    }
    createItemTooltip(instance);
    dragPreviewWindow_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        dragPreviewWindowClass,
        L"HLaunch Drag Preview",
        WS_POPUP,
        0,
        0,
        1,
        1,
        window_,
        nullptr,
        instance,
        this);
    if (!dragPreviewWindow_) {
        return false;
    }
    dragPreviewDpi_ = std::max<UINT>(GetDpiForWindow(dragPreviewWindow_), 96U);
    applyNativeWindowStyle(dragPreviewWindow_, true);
    accessibility_ = std::make_unique<LauncherAccessibility>(LauncherAccessibilityCallbacks{
        .window = window_,
        .snapshot = [this] { return accessibilitySnapshot(); },
        .invoke = [this](const std::wstring_view key) { invokeAccessibleNode(key); },
        .focus = [this](const std::wstring_view key) { focusAccessibleNode(key); },
    });

    const auto effectiveEffects = effectiveWindowEffects(effects);
    translucentSurface_ = effectiveEffects.backdrop
        != platform::windows::WindowBackdrop::Solid;
    static_cast<void>(platform::windows::applyWindowEffects(window_, effectiveEffects));
    static_cast<void>(platform::windows::applyWindowEffects(
        dragPreviewWindow_, effectiveEffects));
    applyNativeWindowStyle(window_, true, true);
    if (!searchWindow_.create(
            instance,
            window_,
            effects,
            [this](const std::wstring_view query) { updateSearch(query); },
            [this](const WPARAM key) { return handleSearchKeyDown(key); })) {
        return false;
    }
    try {
        const HWND notificationWindow = window_;
        dropResolver_ = std::make_unique<platform::windows::DropItemResolver>(
            [notificationWindow](platform::windows::DropImportResult result) {
                auto payload = std::unique_ptr<platform::windows::DropImportResult>{
                    new (std::nothrow) platform::windows::DropImportResult{std::move(result)}};
                if (!payload) {
                    return;
                }
                if (PostMessageW(
                        notificationWindow,
                        dropImportCompletedMessage,
                        0,
                        reinterpret_cast<LPARAM>(payload.get()))) { // NOLINT(performance-no-int-to-ptr): Internal message transfers this heap result to the UI thread.
                    payload.release(); // NOLINT(bugprone-unused-return-value,clang-analyzer-cplusplus.NewDeleteLeaks): The posted UI message owns and deletes the result.
                }
            });
    }
    catch (...) {
        return false;
    }
    if (!dropTarget_.registerForWindow(
            window_,
            [this](std::vector<platform::windows::DroppedSource> sources, const POINTL point) {
                submitDroppedSources(std::move(sources), point);
            })) {
        dropResolver_.reset();
        return false;
    }
    try {
        const HWND notificationWindow = window_;
        iconLoader_ = std::make_unique<platform::windows::IconLoader>(
            [notificationWindow](platform::windows::IconLoadResult result) {
                auto payload = std::unique_ptr<platform::windows::IconLoadResult>{
                    new (std::nothrow) platform::windows::IconLoadResult{std::move(result)}};
                if (!payload) {
                    return;
                }
                if (PostMessageW(
                        notificationWindow,
                        iconLoadCompletedMessage,
                        0,
                        reinterpret_cast<LPARAM>(payload.get()))) { // NOLINT(performance-no-int-to-ptr): Internal message transfers this heap result to the UI thread.
                    payload.release(); // NOLINT(bugprone-unused-return-value,clang-analyzer-cplusplus.NewDeleteLeaks): The posted UI message owns and deletes the result.
                }
            },
            std::move(iconCacheDirectory));
    }
    catch (...) {
        iconLoader_.reset();
    }
    searchVisible_ = showSearch;
    return true;
}

void LauncherWindow::setWindowEffects(
    const platform::windows::WindowEffects& effects)
{
    if (windowEffects_ == effects) {
        return;
    }
    windowEffects_ = effects;
    searchWindow_.setWindowEffects(effects);
    refreshSystemAppearance();
}

void LauncherWindow::refreshSystemAppearance()
{
    if (!window_) {
        return;
    }
    const auto effects = effectiveWindowEffects(windowEffects_);
    translucentSurface_ = effects.backdrop
        != platform::windows::WindowBackdrop::Solid;
    static_cast<void>(platform::windows::applyWindowEffects(window_, effects));
    if (dragPreviewWindow_) {
        static_cast<void>(platform::windows::applyWindowEffects(
            dragPreviewWindow_, effects));
    }
    applyNativeWindowStyle(window_, true, true);
    if (dragPreviewWindow_) {
        applyNativeWindowStyle(dragPreviewWindow_, true);
    }
    static_cast<void>(createTextFormats());
    searchWindow_.refreshSystemAppearance();
    discardDeviceResources();
    discardDragPreviewResources();
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void LauncherWindow::setDocumentChangedHandler(DocumentChangedHandler handler)
{
    documentChangedHandler_ = std::move(handler);
}

void LauncherWindow::setGridSizeChangedHandler(GridSizeChangedHandler handler)
{
    gridSizeChangedHandler_ = std::move(handler);
}

void LauncherWindow::setDeleteConfirmationHandler(DeleteConfirmationHandler handler)
{
    deleteConfirmationHandler_ = std::move(handler);
}

void LauncherWindow::setItemEditorHandler(ItemEditorHandler handler)
{
    itemEditorHandler_ = std::move(handler);
}

void LauncherWindow::setSettingsHandler(SettingsHandler handler)
{
    settingsHandler_ = std::move(handler);
}

void LauncherWindow::show()
{
    if (!isVisible()) {
        positionOnCursorMonitor();
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    if (searchVisible_) {
        positionSearchWindow();
        searchWindow_.show();
    }
    else {
        searchWindow_.hide();
    }
    SetForegroundWindow(window_);
    if (searchVisible_) {
        SetFocus(searchWindow_.handle());
    }
    else {
        SetFocus(window_);
    }
    keyboardSelectionActive_ = false;
    accessibilityFocusKey_ = std::wstring{accessibleRootKey};
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::showAtScreenEdge(const activation::ScreenEdgeHit& hit)
{
    positionOnScreenEdge(hit);
    ShowWindow(window_, SW_SHOWNORMAL);
    if (searchVisible_) {
        positionSearchWindow();
        searchWindow_.show();
    }
    else {
        searchWindow_.hide();
    }
    SetForegroundWindow(window_);
    if (searchVisible_) {
        SetFocus(searchWindow_.handle());
    }
    else {
        SetFocus(window_);
    }
    keyboardSelectionActive_ = false;
    accessibilityFocusKey_ = std::wstring{accessibleRootKey};
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::positionOnCursorMonitor()
{
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        cursor = POINT{};
    }
    const auto monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(MONITORINFO);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return;
    }

    // Moving first lets WM_DPICHANGED update dpi_ before the final size and center
    // are calculated. This avoids an old-DPI SetWindowPos overwriting the suggested
    // rectangle when activation crosses monitors.
    SetWindowPos(
        window_,
        nullptr,
        info.rcWork.left,
        info.rcWork.top,
        0,
        0,
        SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);

    const auto desiredSize = launcherWindowSizePixels();
    const int desiredWidth = desiredSize.cx;
    const int desiredHeight = desiredSize.cy;
    const auto placement = calculateCursorCenteredWindowRectangle(
        RectPixels{info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom},
        cursor.x,
        cursor.y,
        desiredWidth,
        desiredHeight);
    SetWindowPos(
        window_,
        nullptr,
        placement.left,
        placement.top,
        placement.right - placement.left,
        placement.bottom - placement.top,
        SWP_NOACTIVATE | SWP_NOZORDER);
}

void LauncherWindow::positionOnScreenEdge(const activation::ScreenEdgeHit& hit)
{
    SetWindowPos(
        window_,
        nullptr,
        hit.workArea.left,
        hit.workArea.top,
        0,
        0,
        SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);

    const auto desiredSize = launcherWindowSizePixels();
    const int desiredWidth = desiredSize.cx;
    const int desiredHeight = desiredSize.cy;
    const auto placement = calculateScreenEdgeWindowRectangle({
        .workArea = RectPixels{
            hit.workArea.left,
            hit.workArea.top,
            hit.workArea.right,
            hit.workArea.bottom,
        },
        .windowWidth = desiredWidth,
        .windowHeight = desiredHeight,
        .cursorX = hit.cursor.x,
        .cursorY = hit.cursor.y,
        .zone = hit.zone,
    });
    SetWindowPos(
        window_,
        nullptr,
        placement.left,
        placement.top,
        placement.right - placement.left,
        placement.bottom - placement.top,
        SWP_NOACTIVATE | SWP_NOZORDER);
}

SIZE LauncherWindow::launcherWindowSizePixels() const noexcept
{
    const auto sizeDip = calculateLauncherWindowSizeDip({gridColumns_, gridRows_}, metrics_);
    return {
        MulDiv(static_cast<int>(std::lround(sizeDip.width)), static_cast<int>(dpi_), 96),
        MulDiv(static_cast<int>(std::lround(sizeDip.height)), static_cast<int>(dpi_), 96),
    };
}

bool LauncherWindow::applyGridSizeChange(const LauncherGridSize gridSize)
{
    const auto columns = static_cast<std::uint16_t>(std::clamp(
        gridSize.columns,
        static_cast<std::size_t>(core::minimumLauncherGridColumns),
        static_cast<std::size_t>(core::maximumLauncherGridColumns)));
    const auto rows = static_cast<std::uint16_t>(std::clamp(
        gridSize.rows,
        static_cast<std::size_t>(core::minimumLauncherGridRows),
        static_cast<std::size_t>(core::maximumLauncherGridRows)));
    if (columns == gridColumns_ && rows == gridRows_) {
        return true;
    }

    auto updatedDocument = resizeStartDocument_.value_or(document_);
    if (columns != gridColumns_
        && !core::reflowGridColumns(updatedDocument, gridColumns_, columns)) {
        return false;
    }
    if (gridSizeChangedHandler_ && !gridSizeChangedHandler_(columns, rows)) {
        return false;
    }

    std::optional<std::string> focusedItemId{};
    if (keyboardSelectionActive_) {
        if (const auto displayed = displayedItem(focusedItemIndex_);
            displayed && displayed->item) {
            focusedItemId = displayed->item->id;
        }
    }

    const bool columnsChanged = columns != gridColumns_;
    gridColumns_ = columns;
    gridRows_ = rows;
    if (columnsChanged) {
        document_ = std::move(updatedDocument);
        rebuildSearchIndex();
        if (isSearchFiltering()) {
            updateSearch(searchWindow_.query());
        }
        else if (focusedItemId && activeTab()) {
            const auto found = std::ranges::find(
                activeTab()->items, *focusedItemId, &core::LaunchItem::id);
            if (found != activeTab()->items.end() && found->gridSlot) {
                focusedItemIndex_ = *found->gridSlot;
            }
            else {
                keyboardSelectionActive_ = false;
            }
        }
        if (documentChangedHandler_) {
            documentChangedHandler_(document_);
        }
    }
    ensureFocusedItemVisible();
    clearHover();
    if (accessibility_) {
        accessibility_->refresh();
    }
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

void LauncherWindow::hide()
{
    clearHover();
    cancelTabDrag();
    cancelItemDrag();
    searchVisible_ = false;
    searchResults_.clear();
    pageOffset_ = 0;
    wheelDeltaRemainder_ = 0;
    focusedItemIndex_ = 0;
    keyboardSelectionActive_ = false;
    searchWindow_.setQuery({});
    searchWindow_.hide();
    ShowWindow(window_, SW_HIDE);
}

void LauncherWindow::hideAfterSuccessfulLaunchIfNeeded()
{
    if (!windowPinned_) {
        hide();
    }
}

void LauncherWindow::toggle()
{
    if (isVisible()) {
        hide();
    }
    else {
        show();
    }
}

void LauncherWindow::close()
{
    if (window_) {
        DestroyWindow(window_);
    }
}

HWND LauncherWindow::handle() const noexcept
{
    return window_;
}

bool LauncherWindow::isVisible() const noexcept
{
    return window_ && IsWindowVisible(window_);
}

LRESULT CALLBACK LauncherWindow::windowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) noexcept
{
    try {
    LauncherWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam); // NOLINT(performance-no-int-to-ptr): Win32 LPARAM carries this pointer.
        self = static_cast<LauncherWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else {
        self = reinterpret_cast<LauncherWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr): Win32 stores this pointer as LONG_PTR.
    }

    if (self) {
        return self->handleMessage(window, message, wParam, lParam);
    }
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch launcher window callback failed.\n");
        if (message == WM_NCCREATE) return FALSE;
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK LauncherWindow::dragPreviewWindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) noexcept
{
    try {
        LauncherWindow* self = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam); // NOLINT(performance-no-int-to-ptr): Win32 LPARAM carries this pointer.
            self = static_cast<LauncherWindow*>(create->lpCreateParams);
            self->dragPreviewWindow_ = window;
            SetWindowLongPtrW(
                window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else {
            self = reinterpret_cast<LauncherWindow*>(
                GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr): Win32 stores this pointer as LONG_PTR.
        }

        switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return TRUE;
        case WM_SIZE:
            if (self && self->dragPreviewRenderTarget_) {
                self->dragPreviewRenderTarget_->Resize(
                    D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            }
            return 0;
        case WM_DPICHANGED:
            if (self) {
                self->dragPreviewDpi_ = std::max<UINT>(HIWORD(wParam), 1U);
                const auto* suggested = reinterpret_cast<const RECT*>(lParam); // NOLINT(performance-no-int-to-ptr): WM_DPICHANGED defines LPARAM as RECT*.
                SetWindowPos(
                    window,
                    nullptr,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOACTIVATE | SWP_NOZORDER);
                if (self->dragPreviewRenderTarget_) {
                    const auto dpi = static_cast<float>(self->dragPreviewDpi_);
                    self->dragPreviewRenderTarget_->SetDpi(dpi, dpi);
                }
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_PAINT:
            if (self) {
                self->renderDragPreview();
                return 0;
            }
            break;
        case WM_NCDESTROY:
            if (self) {
                self->discardDragPreviewResources();
                self->dragPreviewWindow_ = nullptr;
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch drag preview callback failed.\n");
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT LauncherWindow::handleMessage(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        if (!createDeviceIndependentResources()) {
            return -1;
        }
        return 0;
    case WM_PAINT:
        render();
        ValidateRect(window_, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTALLKEYS;
    case WM_GETOBJECT:
        if (accessibility_) {
            if (const auto result = accessibility_->handleGetObject(wParam, lParam)) return result;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case launcherAccessibilityInvokeMessage:
        if (const auto key = reinterpret_cast<const std::wstring*>(lParam)) {
            invokeAccessibleNode(*key);
        }
        return 0;
    case launcherAccessibilityFocusMessage:
        if (const auto key = reinterpret_cast<const std::wstring*>(lParam)) {
            focusAccessibleNode(*key);
        }
        return 0;
    case WM_SETFOCUS:
        windowFocused_ = true;
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        windowFocused_ = false;
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            scheduleAutoHide();
        }
        else {
            KillTimer(window_, autoHideTimer);
        }
        return 0;
    case WM_TIMER:
        if (wParam == autoHideTimer) {
            KillTimer(window_, autoHideTimer);
            if (!windowPinned_ && isVisible()) {
                DWORD foregroundProcess{};
                GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
                if (foregroundProcess != GetCurrentProcessId()) {
                    hide();
                }
            }
            return 0;
        }
        if (wParam == itemTooltipTimer) {
            showItemTooltip();
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    case WM_KEYDOWN:
        if (handleKeyDown(wParam)) {
            if (keyboardSelectionActive_) {
                if (const auto displayed = displayedItem(focusedItemIndex_); displayed && displayed->item) {
                    accessibilityFocusKey_ = L"item:" + utf8ToWide(displayed->item->id);
                    if (accessibility_) accessibility_->raiseFocusChanged(accessibilityFocusKey_);
                }
            }
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    case WM_SYSKEYDOWN:
        if (wParam == VK_F4) {
            close();
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    case WM_CHAR:
        if (wParam >= 0x20 && wParam != 0x7F) {
            beginSearch(std::wstring(1, static_cast<wchar_t>(wParam)));
        }
        return 0;
    case WM_UNICHAR:
        if (wParam == UNICODE_NOCHAR) {
            return TRUE;
        }
        if (wParam >= 0x20 && wParam <= 0x10FFFF) {
            std::wstring initialText{};
            if (wParam <= 0xFFFF) {
                initialText.push_back(static_cast<wchar_t>(wParam));
            }
            else {
                const auto codePoint = static_cast<unsigned long>(wParam) - 0x10000UL;
                initialText.push_back(static_cast<wchar_t>(0xD800UL + (codePoint >> 10U)));
                initialText.push_back(static_cast<wchar_t>(0xDC00UL + (codePoint & 0x3FFUL)));
            }
            beginSearch(initialText);
        }
        return 0;
    case WM_MOUSEWHEEL:
        handleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_LBUTTONDOWN: {
        RECT client{};
        GetClientRect(window_, &client);
        const float xDip = static_cast<float>(GET_X_LPARAM(lParam)) * 96.0F
            / static_cast<float>(dpi_);
        const float yDip = static_cast<float>(GET_Y_LPARAM(lParam)) * 96.0F
            / static_cast<float>(dpi_);
        const auto layout = calculateLauncherLayout({
            .clientWidthDip = static_cast<float>(client.right) * 96.0F
                / static_cast<float>(dpi_),
            .clientHeightDip = static_cast<float>(client.bottom) * 96.0F
                / static_cast<float>(dpi_),
            .itemCount = displayedTileCount(),
        }, metrics_);
        const auto itemIndex = hitTestLauncherItem(layout, xDip, yDip);
        const bool occupiedItem = itemIndex
            && displayedItem(pageOffset_ + *itemIndex).has_value();
        if (itemIndex && !occupiedItem) {
            emptySlotWindowDragStart_ = POINT{
                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(window_);
            SetFocus(window_);
            return 0;
        }
        if (isSearchFiltering()) {
            return 0;
        }
        if (itemIndex && occupiedItem) {
            beginItemDrag(
                pageOffset_ + *itemIndex,
                POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            SetFocus(window_);
            return 0;
        }
        if (const auto tabIndex = hitTestLauncherTab(
                layout, document_.tabs.size(), xDip, yDip, firstVisibleTabIndex_)) {
            beginTabDrag(
                *tabIndex,
                POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            SetFocus(window_);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (emptySlotWindowDragStart_) {
            if ((wParam & MK_LBUTTON) == 0) {
                emptySlotWindowDragStart_.reset();
                if (GetCapture() == window_) {
                    ReleaseCapture();
                }
                return 0;
            }
            const POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const auto horizontalDistance = std::abs(
                clientPoint.x - emptySlotWindowDragStart_->x);
            const auto verticalDistance = std::abs(
                clientPoint.y - emptySlotWindowDragStart_->y);
            if (horizontalDistance < GetSystemMetrics(SM_CXDRAG)
                && verticalDistance < GetSystemMetrics(SM_CYDRAG)) {
                return 0;
            }
            emptySlotWindowDragStart_.reset();
            if (GetCapture() == window_) {
                ReleaseCapture();
            }
            auto screenPoint = clientPoint;
            ClientToScreen(window_, &screenPoint);
            SendMessageW(
                window_, WM_NCLBUTTONDOWN, HTCAPTION,
                MAKELPARAM(screenPoint.x, screenPoint.y));
            return 0;
        }
        if (tabDragSourceIndex_ && (wParam & MK_LBUTTON) != 0) {
            clearHover();
            updateTabDrag(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        }
        if (itemDragSource_ && (wParam & MK_LBUTTON) != 0) {
            clearHover();
            updateItemDrag(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        }
        updateHover(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;
    case WM_MOUSELEAVE:
        clearHover();
        return 0;
    case WM_LBUTTONDBLCLK:
        // Double-clicking the Grid has no command. Empty slots remain available
        // for window dragging and their context menu, while occupied items keep
        // their existing single-click launch behavior.
        return 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window_, &point);
        RECT client{};
        GetClientRect(window_, &client);
        if (point.x < client.left || point.x >= client.right
            || point.y < client.top || point.y >= client.bottom) {
            return DefWindowProcW(window_, message, wParam, lParam);
        }
        const int resizeBorder = std::max(1, MulDiv(6, static_cast<int>(dpi_), 96));
        const bool left = point.x < client.left + resizeBorder;
        const bool right = point.x >= client.right - resizeBorder;
        const bool top = point.y < client.top + resizeBorder;
        const bool bottom = point.y >= client.bottom - resizeBorder;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        const float xDip = static_cast<float>(point.x) * 96.0F / static_cast<float>(dpi_);
        const float yDip = static_cast<float>(point.y) * 96.0F / static_cast<float>(dpi_);
        const float widthDip = static_cast<float>(client.right) * 96.0F / static_cast<float>(dpi_);
        const float heightDip = static_cast<float>(client.bottom) * 96.0F / static_cast<float>(dpi_);
        const auto layout = calculateLauncherLayout({
            widthDip,
            heightDip,
            displayedTileCount(),
        }, metrics_);
        const auto itemIndex = hitTestLauncherItem(layout, xDip, yDip);
        const bool occupiedItem = itemIndex
            && displayedItem(pageOffset_ + *itemIndex).has_value();
        const bool dragRegion = isLauncherDragRegion(
            layout, xDip, yDip, occupiedItem);
        // Empty cells stay client hit targets so the context menu remains
        // available. WM_LBUTTONDOWN promotes an empty cell to native window
        // movement after the system drag threshold.
        return dragRegion && !itemIndex ? HTCAPTION : HTCLIENT;
    }
    case WM_LBUTTONUP: {
        if (emptySlotWindowDragStart_) {
            emptySlotWindowDragStart_.reset();
            if (GetCapture() == window_) {
                ReleaseCapture();
            }
        }
        std::optional<std::size_t> requiredClickIndex{};
        std::optional<std::size_t> requiredTabClickIndex{};
        const POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (tabDragSourceIndex_) {
            const bool wasDragging = tabDragActive_;
            requiredTabClickIndex = tabDragSourceIndex_;
            if (wasDragging) {
                finishTabDrag(clientPoint);
                return 0;
            }
            cancelTabDrag();
        }
        if (itemDragSource_) {
            const bool wasDragging = itemDragActive_;
            requiredClickIndex = pressedItemIndex_;
            if (wasDragging) {
                finishItemDrag(clientPoint);
                return 0;
            }
            cancelItemDrag();
            if (!requiredClickIndex) {
                return 0;
            }
        }
        RECT client{};
        GetClientRect(window_, &client);
        const float xDip = static_cast<float>(GET_X_LPARAM(lParam)) * 96.0F
            / static_cast<float>(dpi_);
        const float yDip = static_cast<float>(GET_Y_LPARAM(lParam)) * 96.0F
            / static_cast<float>(dpi_);
        const float widthDip = static_cast<float>(client.right) * 96.0F
            / static_cast<float>(dpi_);
        const float heightDip = static_cast<float>(client.bottom) * 96.0F
            / static_cast<float>(dpi_);
        const auto layout = calculateLauncherLayout({
            widthDip,
            heightDip,
            displayedTileCount(),
        }, metrics_);
        const auto& closeButton = layout.closeButton;
        const auto contains = [xDip, yDip](const RectDip& rectangle) {
            return xDip >= rectangle.x && xDip < rectangle.x + rectangle.width
                && yDip >= rectangle.y && yDip < rectangle.y + rectangle.height;
        };
        if (contains(layout.menuButton)) {
            POINT screenPoint{
                static_cast<LONG>(std::lround(layout.menuButton.x)),
                static_cast<LONG>(std::lround(
                    layout.menuButton.y + layout.menuButton.height))};
            ClientToScreen(window_, &screenPoint);
            showLauncherContextMenu(screenPoint);
            return 0;
        }
        if (contains(layout.pinButton)) {
            toggleWindowPin();
            return 0;
        }
        if (xDip >= closeButton.x && xDip < closeButton.x + closeButton.width
            && yDip >= closeButton.y && yDip < closeButton.y + closeButton.height) {
            hide();
            return 0;
        }
        if (!isSearchFiltering()) {
            if (const auto tabIndex = hitTestLauncherTab(
                    layout,
                    document_.tabs.size(),
                    xDip,
                    yDip,
                    firstVisibleTabIndex_)) {
                if (requiredTabClickIndex && *tabIndex != *requiredTabClickIndex) {
                    return 0;
                }
                SetFocus(window_);
                changeActiveTab(*tabIndex);
                return 0;
            }
        }
        if (const auto itemIndex = hitTestLauncherItem(layout, xDip, yDip)) {
            const auto absoluteIndex = pageOffset_ + *itemIndex;
            if (requiredClickIndex && absoluteIndex != *requiredClickIndex) {
                return 0;
            }
            if (const auto displayed = displayedItem(absoluteIndex);
                displayed && displayed->item && launchHandler_) {
                focusedItemIndex_ = absoluteIndex;
                keyboardSelectionActive_ = false;
                SetFocus(window_);
                InvalidateRect(window_, nullptr, FALSE);
                activateFocusedItem();
            }
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (emptySlotWindowDragStart_
            && reinterpret_cast<HWND>(lParam) != window_) { // NOLINT(performance-no-int-to-ptr): WM_CAPTURECHANGED defines LPARAM as HWND.
            emptySlotWindowDragStart_.reset();
        }
        if (tabDragSourceIndex_ && reinterpret_cast<HWND>(lParam) != window_) { // NOLINT(performance-no-int-to-ptr): WM_CAPTURECHANGED defines LPARAM as HWND.
            cancelTabDrag();
        }
        if (itemDragSource_ && reinterpret_cast<HWND>(lParam) != window_) { // NOLINT(performance-no-int-to-ptr): WM_CAPTURECHANGED defines LPARAM as HWND.
            cancelItemDrag();
        }
        return 0;
    case WM_RBUTTONUP: {
        RECT client{};
        GetClientRect(window_, &client);
        const float xDip = static_cast<float>(GET_X_LPARAM(lParam)) * 96.0F / static_cast<float>(dpi_);
        const float yDip = static_cast<float>(GET_Y_LPARAM(lParam)) * 96.0F / static_cast<float>(dpi_);
        const auto layout = calculateLauncherLayout({
            .clientWidthDip = static_cast<float>(client.right) * 96.0F / static_cast<float>(dpi_),
            .clientHeightDip = static_cast<float>(client.bottom) * 96.0F / static_cast<float>(dpi_),
            .itemCount = displayedTileCount(),
        }, metrics_);
        POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(window_, &screenPoint);
        if (const auto itemIndex = hitTestLauncherItem(layout, xDip, yDip)) {
            if (displayedItem(pageOffset_ + *itemIndex)) {
                showItemContextMenu(pageOffset_ + *itemIndex, screenPoint);
            }
            else if (!isSearchFiltering()) {
                showEmptySlotContextMenu(pageOffset_ + *itemIndex, screenPoint);
            }
        }
        else if (const auto tabIndex = hitTestLauncherTab(
                     layout, document_.tabs.size(), xDip, yDip, firstVisibleTabIndex_)) {
            showTabContextMenu(*tabIndex, screenPoint);
        }
        else if (yDip >= layout.header.y
                 && yDip < layout.header.y + layout.header.height) {
            showLauncherContextMenu(screenPoint);
        }
        return 0;
    }
    case dropImportCompletedMessage: {
        auto result = std::unique_ptr<platform::windows::DropImportResult>{
            reinterpret_cast<platform::windows::DropImportResult*>(lParam)}; // NOLINT(performance-no-int-to-ptr): Internal message owns this heap result.
        if (result) {
            applyDropImport(std::move(*result));
        }
        return 0;
    }
    case iconLoadCompletedMessage: {
        auto result = std::unique_ptr<platform::windows::IconLoadResult>{
            reinterpret_cast<platform::windows::IconLoadResult*>(lParam)}; // NOLINT(performance-no-int-to-ptr): Internal message owns this heap result.
        if (result) {
            applyIconLoadResult(std::move(*result));
        }
        return 0;
    }
    case WM_ENTERSIZEMOVE: {
        RECT rectangle{};
        if (GetWindowRect(window_, &rectangle)) {
            resizeStartRectangle_ = rectangle;
        }
        pendingGridSize_ = LauncherGridSize{gridColumns_, gridRows_};
        resizeStartDocument_ = document_;
        resizePreviewColumns_ = gridColumns_;
        return 0;
    }
    case WM_SIZING: {
        auto* rectangle = reinterpret_cast<RECT*>(lParam); // NOLINT(performance-no-int-to-ptr): WM_SIZING defines LPARAM as RECT*.
        const auto edge = [&]() -> std::optional<LauncherResizeEdge> {
            switch (wParam) {
            case WMSZ_LEFT: return LauncherResizeEdge::Left;
            case WMSZ_RIGHT: return LauncherResizeEdge::Right;
            case WMSZ_TOP: return LauncherResizeEdge::Top;
            case WMSZ_BOTTOM: return LauncherResizeEdge::Bottom;
            case WMSZ_TOPLEFT: return LauncherResizeEdge::TopLeft;
            case WMSZ_TOPRIGHT: return LauncherResizeEdge::TopRight;
            case WMSZ_BOTTOMLEFT: return LauncherResizeEdge::BottomLeft;
            case WMSZ_BOTTOMRIGHT: return LauncherResizeEdge::BottomRight;
            default: return std::nullopt;
            }
        }();
        if (!rectangle || !edge) {
            return FALSE;
        }
        const auto snapped = quantizeLauncherSizingRectangle(
            {rectangle->left, rectangle->top, rectangle->right, rectangle->bottom},
            *edge,
            dpi_,
            metrics_);
        *rectangle = RECT{snapped.left, snapped.top, snapped.right, snapped.bottom};
        const float scale = static_cast<float>(dpi_) / 96.0F;
        pendingGridSize_ = calculateLauncherGridSize(
            static_cast<float>(snapped.right - snapped.left) / scale,
            static_cast<float>(snapped.bottom - snapped.top) / scale,
            metrics_);
        if (resizeStartDocument_
            && (!resizePreviewColumns_
                || *resizePreviewColumns_ != pendingGridSize_->columns)) {
            auto preview = *resizeStartDocument_;
            if (core::reflowGridColumns(
                    preview, gridColumns_, pendingGridSize_->columns)) {
                document_ = std::move(preview);
                resizePreviewColumns_ = pendingGridSize_->columns;
                rebuildSearchIndex();
                if (isSearchFiltering()) {
                    updateSearch(searchWindow_.query());
                }
                ensureFocusedItemVisible();
                clearHover();
                if (accessibility_) {
                    accessibility_->refresh();
                }
                InvalidateRect(window_, nullptr, FALSE);
            }
        }
        return TRUE;
    }
    case WM_EXITSIZEMOVE: {
        const bool committed = !pendingGridSize_
            || *pendingGridSize_ == LauncherGridSize{gridColumns_, gridRows_}
            || applyGridSizeChange(*pendingGridSize_);
        if (!committed) {
            if (resizeStartDocument_) {
                document_ = *resizeStartDocument_;
                rebuildSearchIndex();
                if (isSearchFiltering()) {
                    updateSearch(searchWindow_.query());
                }
            }
        }
        if (!committed
            && (*pendingGridSize_ != LauncherGridSize{gridColumns_, gridRows_})
            && resizeStartRectangle_) {
            const auto& rectangle = *resizeStartRectangle_;
            SetWindowPos(
                window_,
                nullptr,
                rectangle.left,
                rectangle.top,
                rectangle.right - rectangle.left,
                rectangle.bottom - rectangle.top,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }
        pendingGridSize_.reset();
        resizeStartRectangle_.reset();
        resizeStartDocument_.reset();
        resizePreviewColumns_.reset();
        positionSearchWindow();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_SIZE:
        if (renderTarget_ && wParam != SIZE_MINIMIZED) {
            renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        }
        if (wParam != SIZE_MINIMIZED) {
            ensureFocusedItemVisible();
            ensureActiveTabVisible();
            positionSearchWindow();
            if (accessibility_) accessibility_->refresh();
        }
        return 0;
    case WM_MOVE:
        positionSearchWindow();
        if (accessibility_) accessibility_->refresh();
        return 0;
    case WM_DPICHANGED: {
        clearHover();
        dpi_ = HIWORD(wParam);
        if (itemTooltip_) {
            SendMessageW(
                itemTooltip_, TTM_SETMAXTIPWIDTH, 0,
                MulDiv(360, static_cast<int>(std::max<UINT>(dpi_, 96U)), 96));
        }
        const auto* suggested = reinterpret_cast<const RECT*>(lParam); // NOLINT(performance-no-int-to-ptr): WM_DPICHANGED defines LPARAM as RECT*.
        const auto desiredSize = launcherWindowSizePixels();
        SetWindowPos(
            window_,
            nullptr,
            suggested->left,
            suggested->top,
            desiredSize.cx,
            desiredSize.cy,
            SWP_NOACTIVATE | SWP_NOZORDER);
        if (renderTarget_) {
            renderTarget_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        }
        refreshSystemAppearance();
        positionSearchWindow();
        if (accessibility_) accessibility_->refresh();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* information = reinterpret_cast<MINMAXINFO*>(lParam); // NOLINT(performance-no-int-to-ptr): WM_GETMINMAXINFO defines LPARAM as MINMAXINFO*.
        const auto minimum = calculateLauncherWindowSizeDip({
            core::minimumLauncherGridColumns,
            core::minimumLauncherGridRows,
        }, metrics_);
        const auto maximum = calculateLauncherWindowSizeDip({
            core::maximumLauncherGridColumns,
            core::maximumLauncherGridRows,
        }, metrics_);
        information->ptMinTrackSize.x = MulDiv(
            static_cast<int>(std::lround(minimum.width)), static_cast<int>(dpi_), 96);
        information->ptMinTrackSize.y = MulDiv(
            static_cast<int>(std::lround(minimum.height)), static_cast<int>(dpi_), 96);
        information->ptMaxTrackSize.x = MulDiv(
            static_cast<int>(std::lround(maximum.width)), static_cast<int>(dpi_), 96);
        information->ptMaxTrackSize.y = MulDiv(
            static_cast<int>(std::lround(maximum.height)), static_cast<int>(dpi_), 96);
        return 0;
    }
    case WM_CLOSE:
        hide();
        return 0;
    case WM_DESTROY:
        hideItemTooltip();
        cancelTabDrag();
        cancelItemDrag();
        shutdownIconServices();
        shutdownDropServices();
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        if (accessibility_) {
            accessibility_->disconnect();
            accessibility_.reset();
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        itemTooltip_ = nullptr;
        if (window_ == window) window_ = nullptr;
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        if (isSystemAppearanceMessage(message)) {
            refreshSystemAppearance();
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

bool LauncherWindow::createDeviceIndependentResources()
{
    if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            d2dFactory_.put()))) {
        return false;
    }
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(writeFactory_.put())))) {
        return false;
    }

    return createTextFormats();
}

bool LauncherWindow::createTextFormats()
{
    if (!writeFactory_) {
        return false;
    }
    titleFormat_ = nullptr;
    bodyFormat_ = nullptr;
    smallFormat_ = nullptr;
    tabFormat_ = nullptr;
    iconFormat_ = nullptr;
    chromeIconFormat_ = nullptr;
    pinIconFormat_ = nullptr;
    const auto systemFontFamily = systemUiFontFamily(dpi_);
    effectiveFontFamily_ = systemFontFamily;
    const auto& fontFamily = effectiveFontFamily_;
    if (FAILED(writeFactory_->CreateTextFormat(
            fontFamily.c_str(),
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            metrics_.titleFontSize,
            L"zh-CN",
            titleFormat_.put()))) {
        return false;
    }
    if (FAILED(writeFactory_->CreateTextFormat(
            fontFamily.c_str(),
            nullptr,
            DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.0F,
            L"zh-CN",
            bodyFormat_.put()))) {
        return false;
    }
    if (FAILED(writeFactory_->CreateTextFormat(
            fontFamily.c_str(),
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0F,
            L"zh-CN",
            smallFormat_.put()))) {
        return false;
    }
    if (FAILED(writeFactory_->CreateTextFormat(
            fontFamily.c_str(),
            nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(metrics_.tabFontWeight),
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            metrics_.tabFontSize,
            L"zh-CN",
            tabFormat_.put()))) {
        return false;
    }
    if (FAILED(writeFactory_->CreateTextFormat(
            fontFamily.c_str(),
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            22.0F,
            L"zh-CN",
            iconFormat_.put()))) {
        return false;
    }
    constexpr wchar_t fluentIconFont[] = L"Segoe Fluent Icons";
    constexpr wchar_t mdl2IconFont[] = L"Segoe MDL2 Assets";
    const auto* chromeIconFont = systemFontFamilyAvailable(writeFactory_.get(), fluentIconFont)
        ? fluentIconFont
        : mdl2IconFont;
    if (FAILED(writeFactory_->CreateTextFormat(
            chromeIconFont,
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            metrics_.chromeIconSize,
            L"en-US",
            chromeIconFormat_.put()))) {
        return false;
    }
    if (FAILED(writeFactory_->CreateTextFormat(
            chromeIconFont,
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            metrics_.pinIconSize,
            L"en-US",
            pinIconFormat_.put()))) {
        return false;
    }
    titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    smallFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    smallFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    tabFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    tabFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    tabFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    chromeIconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    chromeIconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    pinIconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    pinIconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return true;
}

bool LauncherWindow::createDeviceResources()
{
    if (renderTarget_) {
        return true;
    }

    RECT client{};
    GetClientRect(window_, &client);
    const auto size = D2D1::SizeU(
        static_cast<UINT32>(client.right - client.left),
        static_cast<UINT32>(client.bottom - client.top));
    const auto renderTargetProperties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM,
            translucentSurface_ ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE),
        static_cast<float>(dpi_),
        static_cast<float>(dpi_));
    if (FAILED(d2dFactory_->CreateHwndRenderTarget(
            renderTargetProperties,
            D2D1::HwndRenderTargetProperties(window_, size),
            renderTarget_.put()))) {
        return false;
    }

    const auto palette = launcherPalette(isHighContrastEnabled());
    const struct BrushDefinition {
        std::uint32_t color;
        float opacity;
        winrt::com_ptr<ID2D1SolidColorBrush>* destination;
    } brushes[]{
        {palette.background, palette.backgroundAlpha * (translucentSurface_ ? 0.64F : 1.0F), &backgroundBrush_},
        {palette.surface, palette.surfaceAlpha * (translucentSurface_ ? 0.78F : 1.0F), &surfaceBrush_},
        {palette.elevated, palette.elevatedAlpha * (translucentSurface_ ? 0.84F : 1.0F), &elevatedBrush_},
        {palette.accent, palette.accentAlpha, &accentBrush_},
        {palette.tabBackground, palette.tabBackgroundAlpha, &tabBackgroundBrush_},
        {palette.tabHover, palette.tabHoverAlpha, &tabHoverBrush_},
        {palette.tabIndicator, palette.tabIndicatorAlpha, &tabIndicatorBrush_},
        {palette.danger, 1.0F, &dangerBrush_},
        {palette.itemHighlight, 1.0F, &itemHighlightBrush_},
        {palette.text, palette.textAlpha, &textBrush_},
        {palette.textMuted, palette.textMutedAlpha, &mutedTextBrush_},
        {palette.border, palette.borderAlpha * (translucentSurface_ ? 0.58F : 1.0F), &borderBrush_},
    };
    for (const auto& brush : brushes) {
        if (FAILED(renderTarget_->CreateSolidColorBrush(
                D2D1::ColorF(brush.color, brush.opacity),
                brush.destination->put()))) {
            discardDeviceResources();
            return false;
        }
    }
    return true;
}

void LauncherWindow::discardDeviceResources() noexcept
{
    for (auto& cached : iconCache_) {
        cached.second.bitmap = nullptr;
    }
    borderBrush_ = nullptr;
    mutedTextBrush_ = nullptr;
    textBrush_ = nullptr;
    itemHighlightBrush_ = nullptr;
    dangerBrush_ = nullptr;
    tabIndicatorBrush_ = nullptr;
    tabHoverBrush_ = nullptr;
    tabBackgroundBrush_ = nullptr;
    accentBrush_ = nullptr;
    elevatedBrush_ = nullptr;
    surfaceBrush_ = nullptr;
    backgroundBrush_ = nullptr;
    renderTarget_ = nullptr;
}

bool LauncherWindow::createDragPreviewResources()
{
    if (dragPreviewRenderTarget_) {
        return true;
    }
    if (!dragPreviewWindow_ || !d2dFactory_) {
        return false;
    }
    RECT client{};
    GetClientRect(dragPreviewWindow_, &client);
    const auto size = D2D1::SizeU(
        static_cast<UINT32>(std::max<LONG>(1, client.right - client.left)),
        static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top)));
    dragPreviewDpi_ = std::max<UINT>(GetDpiForWindow(dragPreviewWindow_), 1U);
    const auto previewDpi = static_cast<float>(dragPreviewDpi_);
    return SUCCEEDED(d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM,
                translucentSurface_
                    ? D2D1_ALPHA_MODE_PREMULTIPLIED
                    : D2D1_ALPHA_MODE_IGNORE),
            previewDpi,
            previewDpi),
        D2D1::HwndRenderTargetProperties(dragPreviewWindow_, size),
        dragPreviewRenderTarget_.put()));
}

void LauncherWindow::discardDragPreviewResources() noexcept
{
    dragPreviewRenderTarget_ = nullptr;
}

void LauncherWindow::updateDragPreviewWindow(const RectDip& clientBounds)
{
    if (!dragPreviewWindow_ || !window_) {
        return;
    }
    const auto toMainPixels = [this](const float dip) {
        return static_cast<int>(std::lround(
            dip * static_cast<float>(dpi_) / 96.0F));
    };
    POINT screenCenter{
        toMainPixels(clientBounds.x + clientBounds.width / 2.0F),
        toMainPixels(clientBounds.y + clientBounds.height / 2.0F),
    };
    ClientToScreen(window_, &screenCenter);

    // Move a point of the preview into the destination monitor first. PMv2 then
    // updates the preview HWND DPI before its final DIP-sized rectangle is built.
    SetWindowPos(
        dragPreviewWindow_,
        HWND_TOPMOST,
        screenCenter.x,
        screenCenter.y,
        0,
        0,
        SWP_NOACTIVATE | SWP_NOSIZE);
    dragPreviewDpi_ = std::max<UINT>(GetDpiForWindow(dragPreviewWindow_), 1U);
    const auto toPreviewPixels = [this](const float dip) {
        return static_cast<int>(std::lround(
            dip * static_cast<float>(dragPreviewDpi_) / 96.0F));
    };
    const int width = std::max(1, toPreviewPixels(clientBounds.width));
    const int height = std::max(1, toPreviewPixels(clientBounds.height));
    const int left = screenCenter.x - width / 2;
    const int top = screenCenter.y - height / 2;
    wil::unique_hrgn region{CreateRoundRectRgn(
        0, 0, width + 1, height + 1,
        std::max(2, toPreviewPixels(8.0F)),
        std::max(2, toPreviewPixels(8.0F)))};
    if (region) {
        SetWindowRgn(dragPreviewWindow_, region.release(), FALSE);
    }
    SetWindowPos(
        dragPreviewWindow_,
        HWND_TOPMOST,
        left,
        top,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(dragPreviewWindow_, nullptr, FALSE);
    UpdateWindow(dragPreviewWindow_);
}

void LauncherWindow::hideDragPreviewWindow() noexcept
{
    if (dragPreviewWindow_) {
        ShowWindow(dragPreviewWindow_, SW_HIDE);
    }
}

void LauncherWindow::renderDragPreview()
{
    PAINTSTRUCT paint{};
    BeginPaint(dragPreviewWindow_, &paint);
    if (!createDragPreviewResources()) {
        EndPaint(dragPreviewWindow_, &paint);
        return;
    }

    const auto palette = launcherPalette(isHighContrastEnabled());
    winrt::com_ptr<ID2D1SolidColorBrush> surface{};
    winrt::com_ptr<ID2D1SolidColorBrush> border{};
    winrt::com_ptr<ID2D1SolidColorBrush> accent{};
    winrt::com_ptr<ID2D1SolidColorBrush> tabBackground{};
    winrt::com_ptr<ID2D1SolidColorBrush> tabIndicator{};
    winrt::com_ptr<ID2D1SolidColorBrush> text{};
    winrt::com_ptr<ID2D1SolidColorBrush> mutedText{};
    dragPreviewRenderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(
            palette.surface,
            palette.surfaceAlpha
                * (translucentSurface_ ? 0.78F : 1.0F)),
        surface.put());
    dragPreviewRenderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(
            palette.border,
            palette.borderAlpha
                * (translucentSurface_ ? 0.58F : 1.0F)),
        border.put());
    dragPreviewRenderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(palette.accent, palette.accentAlpha), accent.put());
    dragPreviewRenderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(palette.tabBackground, palette.tabBackgroundAlpha),
        tabBackground.put());
    dragPreviewRenderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(palette.tabIndicator, palette.tabIndicatorAlpha), tabIndicator.put());
    dragPreviewRenderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(palette.text, palette.textAlpha), text.put());
    dragPreviewRenderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(palette.textMuted, palette.textMutedAlpha),
        mutedText.put());
    if (!surface || !border || !accent || !tabBackground || !tabIndicator || !text
        || !mutedText) {
        EndPaint(dragPreviewWindow_, &paint);
        return;
    }

    const auto size = dragPreviewRenderTarget_->GetSize();
    const auto bounds = D2D1::RectF(0.0F, 0.0F, size.width, size.height);
    dragPreviewRenderTarget_->BeginDraw();
    dragPreviewRenderTarget_->Clear(translucentSurface_
        ? D2D1::ColorF(0x000000, 0.0F)
        : D2D1::ColorF(palette.background, 1.0F));

    const auto drawPreviewText = [this](
                                     const std::wstring& value,
                                     const D2D1_RECT_F& textBounds,
                                     IDWriteTextFormat* format,
                                     ID2D1Brush* brush) {
        dragPreviewRenderTarget_->DrawTextW(
            value.c_str(),
            static_cast<UINT32>(value.size()),
            format,
            textBounds,
            brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    if (tabDragActive_ && tabDragSourceIndex_
        && *tabDragSourceIndex_ < document_.tabs.size()) {
        dragPreviewRenderTarget_->FillRectangle(bounds, tabBackground.get());
        drawPreviewText(
            utf8ToWide(document_.tabs[*tabDragSourceIndex_].name),
            bounds,
            tabFormat_.get(),
            text.get());
        if (*tabDragSourceIndex_ == activeTabIndex_) {
            const auto underline = std::clamp(
                metrics_.tabUnderlineThickness, 1.0F, size.height);
            dragPreviewRenderTarget_->FillRectangle(
                D2D1::RectF(
                    0.0F, size.height - underline,
                    size.width, size.height),
                tabIndicator.get());
        }
    }
    else if (itemDragActive_ && itemDragSource_
        && itemDragSource_->tabIndex < document_.tabs.size()
        && itemDragSource_->itemIndex
            < document_.tabs[itemDragSource_->tabIndex].items.size()) {
        const auto& item = document_.tabs[itemDragSource_->tabIndex]
            .items[itemDragSource_->itemIndex];
        const auto itemBounds = D2D1::RectF(
            0.5F, 0.5F,
            std::max(0.5F, size.width - 0.5F),
            std::max(0.5F, size.height - 0.5F));
        dragPreviewRenderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(
                itemBounds, metrics_.itemCornerRadius, metrics_.itemCornerRadius),
            surface.get());
        dragPreviewRenderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(
                itemBounds, metrics_.itemCornerRadius, metrics_.itemCornerRadius),
            border.get(),
            1.0F);
        const auto textSpace = metrics_.showItemText ? 20.0F : 0.0F;
        const auto iconSize = std::max(
            1.0F,
            std::min({
                metrics_.itemIconSize,
                std::max(1.0F, size.width - 8.0F),
                std::max(1.0F, size.height - textSpace - 8.0F),
            }));
        const auto iconLeft = (size.width - iconSize) / 2.0F;
        const auto iconTop = std::max(
            4.0F,
            (size.height - textSpace - iconSize) / 2.0F);
        const auto iconBounds = pixelAlignedIconRect(
            iconLeft, iconTop, iconSize, dragPreviewDpi_);
        bool drewRealIcon = false;
        const auto* cached = ensureItemIcon(item, dragPreviewDpi_);
        if (cached && !cached->pixels.empty()
            && cached->width > 0U && cached->height > 0U) {
            winrt::com_ptr<ID2D1Bitmap> bitmap{};
            if (SUCCEEDED(dragPreviewRenderTarget_->CreateBitmap(
                    D2D1::SizeU(cached->width, cached->height),
                    cached->pixels.data(),
                    cached->width * 4U,
                    D2D1::BitmapProperties(
                        D2D1::PixelFormat(
                            DXGI_FORMAT_B8G8R8A8_UNORM,
                            D2D1_ALPHA_MODE_PREMULTIPLIED),
                        static_cast<float>(dragPreviewDpi_),
                        static_cast<float>(dragPreviewDpi_)),
                    bitmap.put()))) {
                dragPreviewRenderTarget_->DrawBitmap(
                    bitmap.get(),
                    iconBounds,
                    1.0F,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                drewRealIcon = true;
            }
        }
        if (!drewRealIcon) {
            winrt::com_ptr<ID2D1SolidColorBrush> iconBrush{};
            dragPreviewRenderTarget_->CreateSolidColorBrush(
                D2D1::ColorF(itemColor(item.type), 0.92F),
                iconBrush.put());
            dragPreviewRenderTarget_->FillRoundedRectangle(
                D2D1::RoundedRect(iconBounds, 8.0F, 8.0F),
                iconBrush ? iconBrush.get() : surface.get());
            drawPreviewText(
                itemGlyph(utf8ToWide(item.name)),
                iconBounds,
                iconFormat_.get(),
                text.get());
        }
        if (metrics_.showItemText) {
            drawPreviewText(
                utf8ToWide(item.name),
                D2D1::RectF(2.0F, size.height - 21.0F, size.width - 2.0F, size.height - 2.0F),
                smallFormat_.get(),
                text.get());
        }
    }

    const auto drawResult = dragPreviewRenderTarget_->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) {
        discardDragPreviewResources();
    }
    EndPaint(dragPreviewWindow_, &paint);
}

void LauncherWindow::positionSearchWindow()
{
    if (!searchWindow_.handle()) {
        return;
    }

    RECT client{};
    GetClientRect(window_, &client);
    const float widthDip = static_cast<float>(client.right - client.left) * 96.0F
        / static_cast<float>(dpi_);
    const float heightDip = static_cast<float>(client.bottom - client.top) * 96.0F
        / static_cast<float>(dpi_);
    const auto launcherLayout = calculateLauncherLayout({
        .clientWidthDip = widthDip,
        .clientHeightDip = heightDip,
            .itemCount = displayedTileCount(),
        }, metrics_);
    searchWindow_.positionAttached(
        window_,
        dpi_,
        calculateSearchPopupLayout(launcherLayout));
}

void LauncherWindow::createItemTooltip(const HINSTANCE instance)
{
    INITCOMMONCONTROLSEX commonControls{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_WIN95_CLASSES,
    };
    InitCommonControls();
    static_cast<void>(InitCommonControlsEx(&commonControls));
    itemTooltip_ = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_,
        nullptr,
        instance,
        nullptr);
    if (!itemTooltip_) {
        return;
    }
    SetWindowPos(
        itemTooltip_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(
        itemTooltip_, TTM_SETMAXTIPWIDTH, 0,
        MulDiv(360, static_cast<int>(std::max<UINT>(dpi_, 96U)), 96));

    TOOLINFOW tool{};
    tool.cbSize = TTTOOLINFOW_V2_SIZE;
    tool.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    tool.hwnd = window_;
    tool.uId = reinterpret_cast<UINT_PTR>(window_);
    tool.lpszText = const_cast<wchar_t*>(L"HLaunch");
    static_cast<void>(SendMessageW(
        itemTooltip_, TTM_ADDTOOLW, 0,
        reinterpret_cast<LPARAM>(&tool)));
}

void LauncherWindow::showItemTooltip()
{
    KillTimer(window_, itemTooltipTimer);
    if (!itemTooltip_ || hoverItemName_.empty() || itemDragActive_ || tabDragActive_) {
        return;
    }
    TOOLINFOW tool{};
    tool.cbSize = TTTOOLINFOW_V2_SIZE;
    tool.hwnd = window_;
    tool.uId = reinterpret_cast<UINT_PTR>(window_);
    SendMessageW(
        itemTooltip_, TTM_TRACKPOSITION, 0,
        MAKELPARAM(itemTooltipPosition_.x, itemTooltipPosition_.y));
    SendMessageW(
        itemTooltip_, TTM_TRACKACTIVATE, TRUE,
        reinterpret_cast<LPARAM>(&tool));

    RECT tooltipBounds{};
    if (!GetWindowRect(itemTooltip_, &tooltipBounds)) {
        return;
    }
    const LONG tooltipWidth = tooltipBounds.right - tooltipBounds.left;
    const LONG tooltipHeight = tooltipBounds.bottom - tooltipBounds.top;
    LONG left = itemTooltipPosition_.x - tooltipWidth / 2;
    LONG top = itemTooltipPosition_.y;

    MONITORINFO monitorInfo{.cbSize = sizeof(MONITORINFO)};
    const auto monitor = MonitorFromPoint(itemTooltipPosition_, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        const LONG maximumLeft = std::max(
            monitorInfo.rcWork.left,
            monitorInfo.rcWork.right - tooltipWidth);
        const LONG maximumTop = std::max(
            monitorInfo.rcWork.top,
            monitorInfo.rcWork.bottom - tooltipHeight);
        left = std::clamp(left, monitorInfo.rcWork.left, maximumLeft);
        top = std::clamp(top, monitorInfo.rcWork.top, maximumTop);
    }
    SetWindowPos(
        itemTooltip_, HWND_TOPMOST, left, top, 0, 0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void LauncherWindow::hideItemTooltip() noexcept
{
    if (window_) {
        KillTimer(window_, itemTooltipTimer);
    }
    if (!itemTooltip_) {
        return;
    }
    TOOLINFOW tool{};
    tool.cbSize = TTTOOLINFOW_V2_SIZE;
    tool.hwnd = window_;
    tool.uId = reinterpret_cast<UINT_PTR>(window_);
    SendMessageW(
        itemTooltip_, TTM_TRACKACTIVATE, FALSE,
        reinterpret_cast<LPARAM>(&tool));
    SendMessageW(itemTooltip_, TTM_POP, 0, 0);
}

void LauncherWindow::updateHover(const POINT clientPoint)
{
    TRACKMOUSEEVENT tracking{
        .cbSize = sizeof(TRACKMOUSEEVENT),
        .dwFlags = TME_LEAVE,
        .hwndTrack = window_,
    };
    TrackMouseEvent(&tracking);

    RECT client{};
    if (!GetClientRect(window_, &client)) return;
    const float scale = 96.0F / static_cast<float>(dpi_);
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = static_cast<float>(client.right) * scale,
        .clientHeightDip = static_cast<float>(client.bottom) * scale,
            .itemCount = displayedTileCount(),
        }, metrics_);
    auto updated = hitTestLauncherHover(
        layout,
        document_.tabs.size(),
        visibleItemCount(),
        isSearchFiltering(),
        static_cast<float>(clientPoint.x) * scale,
        static_cast<float>(clientPoint.y) * scale,
        firstVisibleTabIndex_);
    if (updated.region == LauncherHoverRegion::Item
        && !displayedItem(pageOffset_ + updated.index)) {
        updated = {};
    }
    if (updated != hoverTarget_) {
        hideItemTooltip();
        hoverTarget_ = updated;
        hoverItemName_.clear();
        if (hoverTarget_.region == LauncherHoverRegion::Item) {
            if (const auto displayed = displayedItem(pageOffset_ + hoverTarget_.index);
                displayed && displayed->item) {
                hoverItemName_ = utf8ToWide(displayed->item->name);
                const auto& itemBounds = layout.items[hoverTarget_.index];
                itemTooltipPosition_ = POINT{
                    static_cast<LONG>(std::lround(
                        (itemBounds.x + itemBounds.width / 2.0F)
                        * static_cast<float>(dpi_) / 96.0F)),
                    static_cast<LONG>(std::lround(
                        (itemBounds.y + itemBounds.height)
                        * static_cast<float>(dpi_) / 96.0F))};
                ClientToScreen(window_, &itemTooltipPosition_);
                itemTooltipPosition_.y += MulDiv(4, static_cast<int>(dpi_), 96);
                if (itemTooltip_ && !hoverItemName_.empty()) {
                    TOOLINFOW tool{};
                    tool.cbSize = TTTOOLINFOW_V2_SIZE;
                    tool.hwnd = window_;
                    tool.uId = reinterpret_cast<UINT_PTR>(window_);
                    tool.lpszText = hoverItemName_.data();
                    SendMessageW(
                        itemTooltip_, TTM_UPDATETIPTEXTW, 0,
                        reinterpret_cast<LPARAM>(&tool));
                    SetTimer(window_, itemTooltipTimer, itemTooltipDelayMs, nullptr);
                }
            }
        }
        SetWindowTextW(
            window_, hoverItemName_.empty() ? L"HLaunch" : hoverItemName_.c_str());
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void LauncherWindow::clearHover() noexcept
{
    if (hoverTarget_.region == LauncherHoverRegion::None && hoverItemName_.empty()) return;
    hideItemTooltip();
    hoverTarget_ = {};
    hoverItemName_.clear();
    if (window_) SetWindowTextW(window_, L"HLaunch");
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::ensureActiveTabVisible() noexcept
{
    const auto tabCount = document_.tabs.size();
    if (tabCount == 0U) {
        firstVisibleTabIndex_ = 0U;
        return;
    }
    activeTabIndex_ = std::min(activeTabIndex_, tabCount - 1U);
    if (!window_) {
        firstVisibleTabIndex_ = std::min(firstVisibleTabIndex_, activeTabIndex_);
        return;
    }

    RECT client{};
    if (!GetClientRect(window_, &client)) {
        return;
    }
    const float scale = 96.0F / static_cast<float>(std::max<UINT>(dpi_, 1U));
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = static_cast<float>(client.right - client.left) * scale,
        .clientHeightDip = static_cast<float>(client.bottom - client.top) * scale,
        .itemCount = displayedTileCount(),
    }, metrics_);
    auto viewport = calculateLauncherTabViewport(
        layout, tabCount, firstVisibleTabIndex_);
    if (viewport.visibleCount == 0U) {
        firstVisibleTabIndex_ = 0U;
        return;
    }

    auto requestedFirst = viewport.firstIndex;
    if (activeTabIndex_ < viewport.firstIndex) {
        requestedFirst = activeTabIndex_;
    }
    else if (activeTabIndex_ >= viewport.firstIndex + viewport.visibleCount) {
        requestedFirst = activeTabIndex_ - viewport.visibleCount + 1U;
    }
    viewport = calculateLauncherTabViewport(layout, tabCount, requestedFirst);
    firstVisibleTabIndex_ = viewport.firstIndex;
}

void LauncherWindow::render()
{
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (!createDeviceResources()) {
        EndPaint(window_, &paint);
        return;
    }

    ensureActiveTabVisible();

    const auto renderSize = renderTarget_->GetSize();
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = renderSize.width,
        .clientHeightDip = renderSize.height,
            .itemCount = displayedTileCount(),
        }, metrics_);

    renderTarget_->BeginDraw();
    const auto palette = launcherPalette(isHighContrastEnabled());
    if (translucentSurface_) {
        renderTarget_->Clear(D2D1::ColorF(0x000000, 0.0F));
        renderTarget_->FillRectangle(
            D2D1::RectF(0.0F, 0.0F, renderSize.width, renderSize.height),
            backgroundBrush_.get());
    }
    else {
        renderTarget_->Clear(D2D1::ColorF(palette.background, 1.0F));
    }
    const auto header = toD2dRect(layout.headerContent);
    const auto headerButtonContent = [&](const RectDip& button) {
        return D2D1::RectF(
            button.x,
            layout.headerContent.y,
            button.x + button.width,
            layout.headerContent.y + layout.headerContent.height);
    };
    const auto chromeBrush = [&](const LauncherChromeIcon icon, const bool hovered) {
        switch (launcherChromeIconTone(icon, hovered, windowPinned_)) {
        case LauncherChromeIconTone::Text:
            return textBrush_.get();
        case LauncherChromeIconTone::Accent:
            return accentBrush_.get();
        case LauncherChromeIconTone::Danger:
            return dangerBrush_.get();
        case LauncherChromeIconTone::Muted:
        default:
            return mutedTextBrush_.get();
        }
    };
    const auto menuButton = headerButtonContent(layout.menuButton);
    drawText(
        L"\uE700",
        menuButton,
        chromeIconFormat_.get(),
        chromeBrush(
            LauncherChromeIcon::Menu,
            hoverTarget_.region == LauncherHoverRegion::Menu));
    drawText(
        hoverItemName_.empty() ? std::wstring_view{L"HLaunch"}
                               : std::wstring_view{hoverItemName_},
        D2D1::RectF(
            layout.menuButton.x + layout.menuButton.width,
            header.top,
            layout.pinButton.x,
            header.bottom),
        titleFormat_.get(),
        mutedTextBrush_.get());

    const auto pinButton = headerButtonContent(layout.pinButton);
    drawText(
        windowPinned_ ? L"\uE77A" : L"\uE718",
        pinButton,
        pinIconFormat_.get(),
        chromeBrush(
            LauncherChromeIcon::Pin,
            hoverTarget_.region == LauncherHoverRegion::Pin));
    const auto closeButton = headerButtonContent(layout.closeButton);
    drawText(
        L"\uE711",
        closeButton,
        chromeIconFormat_.get(),
        chromeBrush(
            LauncherChromeIcon::Close,
            hoverTarget_.region == LauncherHoverRegion::Close));

    renderTarget_->FillRectangle(
        D2D1::RectF(0.0F, layout.tabs.y, renderSize.width, renderSize.height),
        tabBackgroundBrush_.get());

    const bool filtering = isSearchFiltering();
    const auto tabCount = document_.tabs.size();
    const auto tabViewport = calculateLauncherTabViewport(
        layout, tabCount, firstVisibleTabIndex_);
    const auto drawTabIndicator = [&](const D2D1_RECT_F& tabRect, ID2D1Brush* brush) {
        if (!brush || metrics_.tabIndicatorStyle == TabIndicatorStyle::None) {
            return;
        }
        const float availableWidth = std::max(0.0F, tabRect.right - tabRect.left);
        const float desiredWidth = metrics_.tabIndicatorWidth > 0.0F
            ? metrics_.tabIndicatorWidth
            : availableWidth * std::clamp(metrics_.tabIndicatorWidthPercent, 10.0F, 100.0F) / 100.0F;
        const float width = std::clamp(desiredWidth, 1.0F, availableWidth);
        const float center = (tabRect.left + tabRect.right) / 2.0F + metrics_.tabIndicatorOffset;
        const float left = std::clamp(center - width / 2.0F, tabRect.left, tabRect.right - width);
        const float right = left + width;
        const float thickness = std::clamp(metrics_.tabUnderlineThickness, 1.0F, layout.tabs.height);
        if (metrics_.tabIndicatorStyle == TabIndicatorStyle::Border) {
            renderTarget_->DrawRoundedRectangle(
                D2D1::RoundedRect(tabRect, metrics_.tabCornerRadius, metrics_.tabCornerRadius),
                brush, thickness);
        }
        else if (metrics_.tabIndicatorStyle == TabIndicatorStyle::Pill) {
            const auto pill = D2D1::RectF(
                left, tabRect.top + metrics_.tabHorizontalInset,
                right, tabRect.bottom - metrics_.tabHorizontalInset);
            renderTarget_->FillRoundedRectangle(
                D2D1::RoundedRect(
                    pill, metrics_.tabIndicatorCornerRadius, metrics_.tabIndicatorCornerRadius),
                brush);
        }
        else {
            const float top = metrics_.tabIndicatorStyle == TabIndicatorStyle::Topline
                ? tabRect.top
                : tabRect.bottom - thickness;
            const auto line = D2D1::RectF(left, top, right, top + thickness);
            renderTarget_->FillRoundedRectangle(
                D2D1::RoundedRect(
                    line, metrics_.tabIndicatorCornerRadius, metrics_.tabIndicatorCornerRadius),
                brush);
        }
    };
    if (filtering) {
        const auto tabRect = toD2dRect(layout.tabs);
        drawText(
            L"搜索结果 · " + std::to_wstring(searchResults_.size()),
            tabRect,
            tabFormat_.get(),
            textBrush_.get());
        drawTabIndicator(tabRect, tabIndicatorBrush_.get());
    }
    else {
        for (std::size_t index = tabViewport.firstIndex;
             index < tabViewport.firstIndex + tabViewport.visibleCount;
             ++index) {
            const auto tabBounds = calculateLauncherTabRect(
                layout, tabCount, index, tabViewport.firstIndex);
            const auto tabRect = toD2dRect(tabBounds);
            const bool hovered = hoverTarget_.region == LauncherHoverRegion::Tab
                && hoverTarget_.index == index;
            const bool selected = index == activeTabIndex_;
            if (hovered) {
                renderTarget_->FillRectangle(tabRect, tabHoverBrush_.get());
            }
            drawText(
                utf8ToWide(document_.tabs[index].name),
                tabRect,
                tabFormat_.get(),
                textBrush_.get());
            if (selected) {
                drawTabIndicator(tabRect, tabIndicatorBrush_.get());
            }
            const bool itemDropTabTarget = itemDragActive_ && itemDropTarget_
                && itemDropTarget_->tabTarget && itemDropTarget_->tabIndex == index;
            if (itemDropTabTarget) {
                renderTarget_->DrawRectangle(
                    D2D1::RectF(
                        tabRect.left + 3.0F,
                        tabRect.top + 3.0F,
                        tabRect.right - 3.0F,
                        tabRect.bottom - 3.0F),
                    accentBrush_.get(),
                    2.0F);
            }
        }
    }
    if (!filtering && tabCount == 0) {
        drawText(
            L"暂无分类",
            toD2dRect(layout.tabs),
            tabFormat_.get(),
            mutedTextBrush_.get());
    }

    const auto gridBounds = toD2dRect(layout.grid);
    renderTarget_->PushAxisAlignedClip(gridBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (std::size_t index = 0; index < layout.items.size(); ++index) {
        const auto absoluteIndex = pageOffset_ + index;
        const auto displayed = displayedItem(absoluteIndex);
        const bool emptySlot = !displayed || !displayed->item;
        const auto name = emptySlot
            ? std::wstring{}
            : utf8ToWide(displayed->item->name);
        const auto glyph = emptySlot ? std::wstring{} : itemGlyph(name);
        const auto color = emptySlot
            ? 0x64748BU
            : itemColor(displayed->item->type);
        const auto tile = toD2dRect(layout.items[index]);
        const bool hovered = !emptySlot
            && hoverTarget_.region == LauncherHoverRegion::Item
            && hoverTarget_.index == index;
        ID2D1Brush* itemBackground = surfaceBrush_.get();
        ID2D1Brush* itemBorder = borderBrush_.get();
        ID2D1Brush* itemText = textBrush_.get();
        float itemBorderWidth = metrics_.itemBorderWidth;
        if (hovered) {
            itemBorder = itemHighlightBrush_.get();
            itemBorderWidth = metrics_.itemHoverBorderWidth;
        }
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(
                tile, metrics_.itemCornerRadius, metrics_.itemCornerRadius),
            itemBackground);
        if (itemBorderWidth > 0.0F) {
            const auto borderBounds = insetRectForInsideStroke(
                layout.items[index], itemBorderWidth);
            renderTarget_->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    toD2dRect(borderBounds),
                    std::max(0.0F, metrics_.itemCornerRadius - itemBorderWidth / 2.0F),
                    std::max(0.0F, metrics_.itemCornerRadius - itemBorderWidth / 2.0F)),
                itemBorder, itemBorderWidth);
        }
        if (itemDragActive_ && itemDropTarget_ && !itemDropTarget_->tabTarget
            && itemDropTarget_->tabIndex == activeTabIndex_
            && itemDropTarget_->displayedTileIndex == index) {
            const auto dropBorderBounds = insetRectForInsideStroke(
                layout.items[index], metrics_.itemDropBorderWidth);
            renderTarget_->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    toD2dRect(dropBorderBounds),
                    std::max(
                        0.0F,
                        metrics_.itemCornerRadius - metrics_.itemDropBorderWidth / 2.0F),
                    std::max(
                        0.0F,
                        metrics_.itemCornerRadius - metrics_.itemDropBorderWidth / 2.0F)),
                accentBrush_.get(),
                metrics_.itemDropBorderWidth);
        }
        const auto focusedWindow = GetFocus();
        const bool keyboardFocused = windowFocused_
            || focusedWindow == searchWindow_.handle()
            || (searchWindow_.handle() && IsChild(searchWindow_.handle(), focusedWindow));
        if (!emptySlot && keyboardSelectionActive_ && keyboardFocused
            && absoluteIndex == focusedItemIndex_) {
            const auto focusBounds = D2D1::RectF(
                tile.left + 2.0F,
                tile.top + 2.0F,
                tile.right - 2.0F,
                tile.bottom - 2.0F);
            renderTarget_->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    focusBounds,
                    std::max(0.0F, metrics_.itemCornerRadius - 2.0F),
                    std::max(0.0F, metrics_.itemCornerRadius - 2.0F)),
                itemHighlightBrush_.get(),
                metrics_.itemFocusBorderWidth);
        }

        if (emptySlot) {
            continue;
        }

        const auto textSpace = metrics_.showItemText ? 20.0F : 0.0F;
        const auto iconSize = std::max(
            1.0F,
            std::min({
                metrics_.itemIconSize,
                std::max(1.0F, tile.right - tile.left - 8.0F),
                std::max(1.0F, tile.bottom - tile.top - textSpace - 8.0F),
            }));
        const auto iconLeft = (tile.left + tile.right - iconSize) / 2.0F;
        const auto iconTop = tile.top + std::max(
            4.0F,
            ((tile.bottom - tile.top) - textSpace - iconSize) / 2.0F);
        const auto iconRect = pixelAlignedIconRect(
            iconLeft, iconTop, iconSize, dpi_);
        auto* realIcon = displayed && displayed->item
            ? itemIconBitmap(*displayed->item)
            : nullptr;
        if (realIcon) {
            renderTarget_->DrawBitmap(
                realIcon,
                iconRect,
                1.0F,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        else {
            winrt::com_ptr<ID2D1SolidColorBrush> iconBrush{};
            renderTarget_->CreateSolidColorBrush(
                D2D1::ColorF(color, 0.92F),
                iconBrush.put());
            renderTarget_->FillRoundedRectangle(
                D2D1::RoundedRect(iconRect, 8.0F, 8.0F),
                iconBrush ? iconBrush.get() : elevatedBrush_.get());
            drawText(
                glyph,
                iconRect,
                iconFormat_.get(),
                itemText);
        }
        if (!metrics_.showItemText) {
            continue;
        }
        drawText(
            name,
            D2D1::RectF(
                tile.left + 2.0F,
                tile.bottom - 21.0F,
                tile.right - 2.0F,
                tile.bottom - 2.0F),
            smallFormat_.get(),
            itemText);
    }
    renderTarget_->PopAxisAlignedClip();

    if (tabDragActive_ && tabDragSourceIndex_ && tabDragTargetIndex_
        && tabDragInsertionIndex_ && tabDragInsertionXDip_ && !document_.tabs.empty()) {
        const float insertionX = std::clamp(
            *tabDragInsertionXDip_,
            layout.tabs.x + 1.5F,
            layout.tabs.x + layout.tabs.width - 1.5F);
        renderTarget_->FillRectangle(
            D2D1::RectF(
                insertionX - 1.5F,
                layout.tabs.y + 3.0F,
                insertionX + 1.5F,
                layout.tabs.y + layout.tabs.height - 3.0F),
            accentBrush_.get());
    }

    const auto drawResult = renderTarget_->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
    }
    EndPaint(window_, &paint);
}

bool LauncherWindow::handleKeyDown(const WPARAM key)
{
    if (key == VK_SPACE && GetKeyState(VK_CONTROL) < 0) {
        toggleWindowPin();
        return true;
    }
    if (key == L'O' && GetKeyState(VK_CONTROL) < 0) {
        if (settingsHandler_) {
            settingsHandler_();
        }
        return true;
    }
    if (key == VK_F4 && GetKeyState(VK_MENU) < 0) {
        close();
        return true;
    }
    if (key == VK_INSERT && !isSearchFiltering()) {
        showAddEditor();
        return true;
    }
    if (key == VK_F2 && keyboardSelectionActive_ && totalItemCount() > 0) {
        showEditEditor(focusedItemIndex_);
        return true;
    }
    if (key == VK_DELETE && keyboardSelectionActive_ && totalItemCount() > 0) {
        deleteItem(focusedItemIndex_);
        return true;
    }
    if (key == L'F' && GetKeyState(VK_CONTROL) < 0) {
        beginSearch();
        return true;
    }
    if (key == VK_ESCAPE) {
        hide();
        return true;
    }
    if (key == VK_RETURN) {
        if (keyboardSelectionActive_) {
            activateFocusedItem();
        }
        return true;
    }
    if (key == VK_TAB) {
        if (const auto tabIndex = cycleLauncherTab(
                activeTabIndex_,
                document_.tabs.size(),
                GetKeyState(VK_SHIFT) < 0)) {
            changeActiveTab(*tabIndex);
        }
        return true;
    }

    const auto itemCount = totalItemCount();
    const auto capacity = pageCapacity();
    if (key == VK_PRIOR || key == VK_NEXT) {
        if (itemCount > 0 && capacity > 0) {
            keyboardSelectionActive_ = true;
            if (key == VK_PRIOR) {
                const auto requested = focusedItemIndex_ > capacity
                    ? focusedItemIndex_ - capacity
                    : 0U;
                focusedItemIndex_ = nearestDisplayedItemIndex(requested, false)
                    .value_or(focusedItemIndex_);
            }
            else {
                const auto requested = std::min(
                    focusedItemIndex_ + capacity,
                    itemCount - 1U);
                focusedItemIndex_ = nearestDisplayedItemIndex(requested, true)
                    .value_or(focusedItemIndex_);
            }
            ensureFocusedItemVisible();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return true;
    }
    if (key == VK_HOME || key == VK_END) {
        if (const auto visibleCount = visibleItemCount(); visibleCount > 0) {
            keyboardSelectionActive_ = true;
            const auto requested = key == VK_HOME
                ? pageOffset_
                : pageOffset_ + visibleCount - 1U;
            focusedItemIndex_ = nearestDisplayedItemIndex(
                requested, key == VK_HOME).value_or(focusedItemIndex_);
            InvalidateRect(window_, nullptr, FALSE);
        }
        return true;
    }

    std::optional<GridNavigationDirection> direction{};
    switch (key) {
    case VK_LEFT:
        direction = GridNavigationDirection::Left;
        break;
    case VK_RIGHT:
        direction = GridNavigationDirection::Right;
        break;
    case VK_UP:
        direction = GridNavigationDirection::Up;
        break;
    case VK_DOWN:
        direction = GridNavigationDirection::Down;
        break;
    default:
        return false;
    }

    if (!keyboardSelectionActive_) {
        if (itemCount > 0) {
            keyboardSelectionActive_ = true;
            focusedItemIndex_ = nearestDisplayedItemIndex(pageOffset_).value_or(0U);
            ensureFocusedItemVisible();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return true;
    }

    RECT client{};
    GetClientRect(window_, &client);
    const float widthDip = static_cast<float>(client.right - client.left) * 96.0F
        / static_cast<float>(dpi_);
    const float heightDip = static_cast<float>(client.bottom - client.top) * 96.0F
        / static_cast<float>(dpi_);
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = widthDip,
        .clientHeightDip = heightDip,
            .itemCount = displayedTileCount(),
        }, metrics_);
    auto candidate = focusedItemIndex_;
    for (std::size_t attempt = 0; attempt < itemCount; ++attempt) {
        const auto itemIndex = navigateGridItem(
            candidate,
            itemCount,
            layout.columns,
            *direction);
        if (!itemIndex || *itemIndex == candidate) {
            break;
        }
        candidate = *itemIndex;
        if (displayedItem(candidate)) {
            focusedItemIndex_ = candidate;
            ensureFocusedItemVisible();
            InvalidateRect(window_, nullptr, FALSE);
            break;
        }
    }
    return true;
}

bool LauncherWindow::handleSearchKeyDown(const WPARAM key)
{
    if (key == VK_ESCAPE) {
        if (!searchWindow_.query().empty()) {
            searchWindow_.setQuery({});
        }
        else {
            hide();
        }
        return true;
    }
    if (key == VK_RETURN) {
        if (keyboardSelectionActive_) {
            activateFocusedItem();
        }
        return true;
    }
    return handleKeyDown(key);
}

void LauncherWindow::beginSearch(const std::wstring_view initialText)
{
    searchVisible_ = true;
    positionSearchWindow();
    searchWindow_.show();

    if (!initialText.empty()) {
        std::wstring query{searchWindow_.query()};
        query.append(initialText);
        searchWindow_.setQuery(std::move(query));
    }
    else {
        updateSearch(searchWindow_.query());
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::updateSearch(const std::wstring_view query)
{
    clearHover();
    searchResults_.clear();
    if (!query.empty()) {
        if (const auto normalizedQuery = platform::windows::normalizeSearchText(query)) {
            searchResults_ = searchIndex_.search(*normalizedQuery, searchIndex_.size());
        }
    }
    focusedItemIndex_ = 0;
    keyboardSelectionActive_ = !searchResults_.empty();
    pageOffset_ = 0;
    wheelDeltaRemainder_ = 0;
    accessibilityFocusKey_ = std::wstring{accessibleRootKey};
    if (accessibility_) accessibility_->raiseStructureChanged();
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::handleMouseWheel(const short delta)
{
    wheelDeltaRemainder_ += delta;
    const int steps = wheelDeltaRemainder_ / WHEEL_DELTA;
    wheelDeltaRemainder_ %= WHEEL_DELTA;
    if (steps == 0) {
        return;
    }

    if (isSearchFiltering() || document_.tabs.size() <= 1U) {
        return;
    }

    const int direction = steps > 0 ? -1 : 1;
    std::size_t target = activeTabIndex_;
    for (int count = 0; count < std::abs(steps); ++count) {
        if (const auto next = cycleLauncherTab(
                target, document_.tabs.size(), direction < 0)) {
            target = *next;
        }
    }
    changeActiveTab(target);
}

void LauncherWindow::showAddEditor(const std::optional<std::size_t> targetGridSlot)
{
    const auto sourceTabIndex = activeTabIndex_;
    auto edited = itemEditorHandler_
        ? itemEditorHandler_(window_, document_.tabs, activeTabIndex_, nullptr)
        : ItemEditorDialog::show(
            window_, document_.tabs, activeTabIndex_, nullptr);
    if (!edited) {
        return;
    }
    auto id = platform::windows::createUuidV4();
    if (!id) {
        showTaskMessage(
            window_, L"HLaunch 条目", L"无法生成条目标识。",
            TaskDialogIcon::Error);
        return;
    }
    edited->item.id = std::move(*id);
    auto portableItem = platform::windows::makeItemPathsPortable(
        edited->item, itemPathContext_);
    if (!portableItem) {
        showTaskMessage(
            window_, L"HLaunch 条目路径", L"无法保存条目路径。",
            TaskDialogIcon::Warning, L"请检查目标、工作目录和图标路径。");
        return;
    }
    edited->item = std::move(*portableItem);
    auto updatedDocument = document_;
    const auto location = core::addItem(
        updatedDocument,
        edited->tabIndex,
        std::move(edited->item),
        edited->tabIndex == sourceTabIndex ? targetGridSlot : std::nullopt);
    if (!location || !core::validateItemsDocument(updatedDocument).empty()) {
        showTaskMessage(
            window_, L"HLaunch 条目", L"条目内容未通过校验。",
            TaskDialogIcon::Warning, L"请检查输入内容后重试。");
        return;
    }
    document_ = std::move(updatedDocument);
    activeTabIndex_ = location->tabIndex;
    focusedItemIndex_ = core::gridSlotForItem(
        document_.tabs[location->tabIndex], location->itemIndex).value_or(0U);
    pageOffset_ = 0;
    searchVisible_ = false;
    searchWindow_.setQuery({});
    searchWindow_.hide();
    rebuildSearchIndex();
    ensureFocusedItemVisible();
    if (documentChangedHandler_) documentChangedHandler_(document_);
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::showEditEditor(const std::size_t absoluteIndex)
{
    const auto resolvedSource = itemLocationForDisplayedIndex(absoluteIndex);
    if (!resolvedSource) return;
    const auto source = *resolvedSource;
    if (source.tabIndex >= document_.tabs.size()
        || source.itemIndex >= document_.tabs[source.tabIndex].items.size()) return;
    const auto* initial = &document_.tabs[source.tabIndex].items[source.itemIndex];
    auto edited = itemEditorHandler_
        ? itemEditorHandler_(window_, document_.tabs, source.tabIndex, initial)
        : ItemEditorDialog::show(
            window_, document_.tabs, source.tabIndex, initial);
    if (!edited) return;

    auto portableItem = platform::windows::makeItemPathsPortable(
        edited->item, itemPathContext_);
    if (!portableItem) {
        showTaskMessage(
            window_, L"HLaunch 条目路径", L"无法保存条目路径。",
            TaskDialogIcon::Warning, L"请检查目标、工作目录和图标路径。");
        return;
    }
    edited->item = std::move(*portableItem);

    auto updatedDocument = document_;
    const auto location = core::updateItem(
        updatedDocument,
        source,
        edited->tabIndex,
        std::move(edited->item));
    if (!location || !core::validateItemsDocument(updatedDocument).empty()) {
        showTaskMessage(
            window_, L"HLaunch 条目", L"条目内容未通过校验。",
            TaskDialogIcon::Warning, L"请检查输入内容后重试。");
        return;
    }
    document_ = std::move(updatedDocument);
    activeTabIndex_ = location->tabIndex;
    focusedItemIndex_ = core::gridSlotForItem(
        document_.tabs[location->tabIndex], location->itemIndex).value_or(0U);
    pageOffset_ = 0;
    searchVisible_ = false;
    searchWindow_.setQuery({});
    searchWindow_.hide();
    rebuildSearchIndex();
    ensureFocusedItemVisible();
    if (documentChangedHandler_) documentChangedHandler_(document_);
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::showItemContextMenu(
    const std::size_t absoluteIndex,
    const POINT screenPoint)
{
    const auto itemLocation = itemLocationForDisplayedIndex(absoluteIndex);
    if (!itemLocation) {
        return;
    }
    focusedItemIndex_ = absoluteIndex;
    keyboardSelectionActive_ = false;
    ensureFocusedItemVisible();
    SetFocus(window_);
    InvalidateRect(window_, nullptr, FALSE);

    auto menu = createItemContextMenu(document_, *itemLocation);
    if (!menu) {
        return;
    }

    SetForegroundWindow(window_);
    const auto selected = TrackPopupMenuEx(
        menu.get(),
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_WORKAREA,
        screenPoint.x,
        screenPoint.y,
        window_,
        nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    if (selected == static_cast<UINT>(ItemContextCommand::Open)) {
        activateFocusedItem();
    }
    else if (selected == static_cast<UINT>(ItemContextCommand::RunAsAdministrator)) {
        const auto& storedItem = document_.tabs[itemLocation->tabIndex].items[itemLocation->itemIndex];
        if (auto item = resolveItemForUse(storedItem, L"以管理员身份启动该条目")) {
            item->runAsAdministrator = true;
            if (launchHandler_) {
                launchHandler_(*item);
            }
        }
    }
    else if (selected == static_cast<UINT>(ItemContextCommand::OpenLocation)) {
        const auto& storedItem = document_.tabs[itemLocation->tabIndex].items[itemLocation->itemIndex];
        const auto item = resolveItemForUse(storedItem, L"解析该条目的所在位置");
        if (!item) {
            return;
        }
        const auto result = platform::windows::openItemLocation(*item);
        if (!result) {
            showTaskMessage(
                window_,
                L"HLaunch 条目位置",
                L"无法打开该条目的所在位置。",
                TaskDialogIcon::Error,
                L"请确认目标仍然存在且可以访问。\n\n系统错误码："
                    + std::to_wstring(result.error().systemCode));
        }
    }
    else if (selected == static_cast<UINT>(ItemContextCommand::CopyName)
        || selected == static_cast<UINT>(ItemContextCommand::CopyTarget)
        || selected == static_cast<UINT>(ItemContextCommand::CopyCommandLine)) {
        const auto& storedItem = document_.tabs[itemLocation->tabIndex].items[itemLocation->itemIndex];
        std::wstring text{};
        bool textValid = true;
        if (selected == static_cast<UINT>(ItemContextCommand::CopyName)) {
            text = utf8ToWide(storedItem.name);
        }
        else {
            const auto item = resolveItemForUse(storedItem, L"解析该条目的路径");
            if (!item) {
                return;
            }
            if (selected == static_cast<UINT>(ItemContextCommand::CopyTarget)) {
                text = utf8ToWide(item->target);
            }
            else {
                const auto commandLine = platform::windows::buildShellCommandLine(*item);
                if (commandLine) {
                    text = *commandLine;
                }
                else {
                    textValid = false;
                }
            }
        }
        if (!textValid || !copyUnicodeTextToClipboard(window_, text)) {
            showTaskMessage(
                window_,
                L"HLaunch 复制",
                L"无法把条目信息复制到剪贴板。",
                TaskDialogIcon::Error,
                L"请稍后重试。");
        }
    }
    else if (selected == static_cast<UINT>(ItemContextCommand::Properties)) {
        showEditEditor(absoluteIndex);
    }
    else if (selected == static_cast<UINT>(ItemContextCommand::Insert)) {
        showAddEditor();
    }
    else if (selected == static_cast<UINT>(ItemContextCommand::Delete)) {
        deleteItem(absoluteIndex);
    }
    else if (const auto targetTabIndex = moveToTabIndexFromMenuCommand(
                 selected,
                 document_.tabs.size())) {
        moveItemToTab(absoluteIndex, *targetTabIndex);
    }
}

void LauncherWindow::showLauncherContextMenu(const POINT screenPoint)
{
    auto menu = createLauncherContextMenu(windowPinned_);
    if (!menu) {
        return;
    }
    SetForegroundWindow(window_);
    const auto selected = TrackPopupMenuEx(
        menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_WORKAREA,
        screenPoint.x, screenPoint.y, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    switch (static_cast<LauncherContextCommand>(selected)) {
    case LauncherContextCommand::TogglePin:
        toggleWindowPin();
        break;
    case LauncherContextCommand::Search:
        beginSearch();
        break;
    case LauncherContextCommand::AddPage:
        addPage();
        break;
    case LauncherContextCommand::Settings:
        if (settingsHandler_) {
            settingsHandler_();
        }
        break;
    case LauncherContextCommand::Exit:
        close();
        break;
    default:
        break;
    }
}

void LauncherWindow::showEmptySlotContextMenu(
    const std::size_t gridSlot,
    const POINT screenPoint)
{
    auto menu = createEmptySlotContextMenu();
    if (!menu) {
        return;
    }
    SetForegroundWindow(window_);
    const auto selected = TrackPopupMenuEx(
        menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_WORKAREA,
        screenPoint.x, screenPoint.y, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    if (selected == static_cast<UINT>(EmptySlotContextCommand::RegisterItem)
        || selected == static_cast<UINT>(EmptySlotContextCommand::InsertSlot)) {
        showAddEditor(gridSlot);
    }
}

void LauncherWindow::showTabContextMenu(
    const std::size_t tabIndex,
    const POINT screenPoint)
{
    if (tabIndex >= document_.tabs.size()) {
        return;
    }
    changeActiveTab(tabIndex);
    auto menu = createTabContextMenu(
        tabIndex,
        document_.tabs.size(),
        !document_.tabs[tabIndex].items.empty());
    if (!menu) {
        return;
    }
    SetForegroundWindow(window_);
    const auto selected = TrackPopupMenuEx(
        menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_WORKAREA,
        screenPoint.x, screenPoint.y, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    switch (static_cast<TabContextCommand>(selected)) {
    case TabContextCommand::AddPage:
        addPage();
        break;
    case TabContextCommand::DeletePage:
        deletePage(tabIndex);
        break;
    case TabContextCommand::MovePageLeft:
        if (tabIndex > 0U) {
            movePage(tabIndex, tabIndex - 1U);
        }
        break;
    case TabContextCommand::MovePageRight:
        if (tabIndex + 1U < document_.tabs.size()) {
            movePage(tabIndex, tabIndex + 1U);
        }
        break;
    case TabContextCommand::LaunchAll:
        launchAllInPage(tabIndex);
        break;
    case TabContextCommand::Rename:
        renamePage(tabIndex);
        break;
    default:
        break;
    }
}

void LauncherWindow::launchAllInPage(const std::size_t tabIndex)
{
    if (!launchHandler_ || tabIndex >= document_.tabs.size()) {
        return;
    }
    const auto items = document_.tabs[tabIndex].items;
    for (const auto& storedItem : items) {
        if (const auto item = resolveItemForUse(storedItem, L"启动页面中的项目")) {
            launchHandler_(*item);
        }
    }
}

void LauncherWindow::addPage()
{
    const auto entered = showTextPromptDialog(
        window_, L"添加页面", L"页面名称：", L"新页面");
    if (!entered) {
        return;
    }
    const auto name = wideToUtf8(*entered);
    auto id = platform::windows::createUuidV4();
    if (!name || name->empty() || !id) {
        showTaskMessage(
            window_, L"HLaunch 页面", L"无法创建页面。",
            TaskDialogIcon::Error);
        return;
    }
    auto updated = document_;
    updated.tabs.push_back(core::Tab{.id = std::move(*id), .name = *name});
    if (!core::validateItemsDocument(updated).empty()) {
        showTaskMessage(
            window_, L"HLaunch 页面", L"页面名称无效或页面数量已达到上限。",
            TaskDialogIcon::Warning);
        return;
    }
    document_ = std::move(updated);
    activeTabIndex_ = document_.tabs.size() - 1U;
    focusedItemIndex_ = 0;
    pageOffset_ = 0;
    rebuildSearchIndex();
    if (documentChangedHandler_) {
        documentChangedHandler_(document_);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::deletePage(const std::size_t tabIndex)
{
    const auto disposition = tabDeleteDisposition(document_, tabIndex);
    if (disposition == TabDeleteDisposition::Unavailable) {
        return;
    }
    const auto& page = document_.tabs[tabIndex];
    if (disposition == TabDeleteDisposition::ConfirmationRequired) {
        std::wstring prompt = L"确定删除页面“" + utf8ToWide(page.name) + L"”吗？";
        const std::size_t target = tabIndex == 0 ? 1U : 0U;
        prompt += L"\n\n页面中的项目将移动到“";
        prompt += utf8ToWide(document_.tabs[target].name);
        prompt += L"”。";
        if (!confirmTask(window_, L"HLaunch 删除页面", prompt)) {
            return;
        }
    }

    auto updated = document_;
    if (!updated.tabs[tabIndex].items.empty()) {
        const std::size_t target = tabIndex == 0 ? 1U : 0U;
        auto& destination = updated.tabs[target].items;
        auto& source = updated.tabs[tabIndex].items;
        auto nextGridSlot = core::gridSlotExtent(updated.tabs[target]);
        for (auto& item : source) {
            item.gridSlot = static_cast<std::uint32_t>(nextGridSlot++);
        }
        destination.insert(
            destination.end(),
            std::make_move_iterator(source.begin()),
            std::make_move_iterator(source.end()));
    }
    updated.tabs.erase(updated.tabs.begin() + static_cast<std::ptrdiff_t>(tabIndex));
    if (!core::validateItemsDocument(updated).empty()) {
        showTaskMessage(
            window_, L"HLaunch 页面", L"无法安全删除该页面。",
            TaskDialogIcon::Error);
        return;
    }
    document_ = std::move(updated);
    activeTabIndex_ = 0;
    focusedItemIndex_ = 0;
    pageOffset_ = 0;
    rebuildSearchIndex();
    if (documentChangedHandler_) {
        documentChangedHandler_(document_);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::renamePage(const std::size_t tabIndex)
{
    if (tabIndex >= document_.tabs.size()) {
        return;
    }
    const auto entered = showTextPromptDialog(
        window_, L"重命名页面", L"页面名称：",
        utf8ToWide(document_.tabs[tabIndex].name));
    if (!entered) {
        return;
    }
    const auto name = wideToUtf8(*entered);
    if (!name || name->empty()) {
        return;
    }
    auto updated = document_;
    updated.tabs[tabIndex].name = *name;
    if (!core::validateItemsDocument(updated).empty()) {
        showTaskMessage(
            window_, L"HLaunch 页面", L"页面名称无效。",
            TaskDialogIcon::Warning);
        return;
    }
    document_ = std::move(updated);
    rebuildSearchIndex();
    if (documentChangedHandler_) {
        documentChangedHandler_(document_);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::movePage(
    const std::size_t tabIndex,
    const std::size_t targetTabIndex)
{
    auto updated = document_;
    const auto movedIndex = core::moveTab(updated, tabIndex, targetTabIndex);
    if (!movedIndex || !core::validateItemsDocument(updated).empty()) {
        showTaskMessage(
            window_, L"HLaunch 页面排序", L"无法移动该页面。",
            TaskDialogIcon::Error, L"数据未发生变化。");
        return;
    }

    document_ = std::move(updated);
    activeTabIndex_ = *movedIndex;
    accessibilityFocusKey_ = L"tab:" + utf8ToWide(document_.tabs[*movedIndex].id);
    rebuildSearchIndex();
    if (documentChangedHandler_) {
        documentChangedHandler_(document_);
    }
    if (accessibility_) {
        accessibility_->raiseStructureChanged();
        accessibility_->raiseSelectionChanged(accessibilityFocusKey_);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::toggleWindowPin()
{
    windowPinned_ = !windowPinned_;
    KillTimer(window_, autoHideTimer);
    SetWindowPos(
        window_,
        windowPinned_ ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::scheduleAutoHide()
{
    if (!windowPinned_ && isVisible()) {
        SetTimer(window_, autoHideTimer, 80, nullptr);
    }
}

void LauncherWindow::deleteItem(const std::size_t absoluteIndex)
{
    const auto source = itemLocationForDisplayedIndex(absoluteIndex);
    if (!source || source->tabIndex >= document_.tabs.size()
        || source->itemIndex >= document_.tabs[source->tabIndex].items.size()) {
        return;
    }

    const auto& item = document_.tabs[source->tabIndex].items[source->itemIndex];
    std::wstring prompt = L"确定删除“";
    prompt.append(utf8ToWide(item.name));
    prompt.append(L"”吗？\n\n此操作会从 HLaunch 中移除该条目。");
    const bool confirmed = deleteConfirmationHandler_
        ? deleteConfirmationHandler_(window_, item)
        : confirmTask(window_, L"HLaunch 删除条目", prompt);
    if (!confirmed) {
        if (isSearchFiltering()) {
            searchWindow_.show();
        }
        return;
    }

    const bool wasFiltering = isSearchFiltering();
    const std::wstring query{searchWindow_.query()};
    const auto previousFocus = focusedItemIndex_;
    auto updatedDocument = document_;
    const auto removed = core::removeItem(updatedDocument, *source);
    if (!removed || !core::validateItemsDocument(updatedDocument).empty()) {
        showTaskMessage(
            window_, L"HLaunch 删除条目", L"无法删除该条目。",
            TaskDialogIcon::Error, L"数据未发生变化。");
        return;
    }

    document_ = std::move(updatedDocument);
    if (iconLoader_) {
        iconLoader_->invalidate(removed->id);
    }
    std::erase_if(iconCache_, [&removed](const auto& entry) {
        return entry.first.itemId == removed->id;
    });
    rebuildSearchIndex();
    if (wasFiltering) {
        updateSearch(query);
        if (!searchResults_.empty()) {
            focusedItemIndex_ = std::min(previousFocus, searchResults_.size() - 1U);
        }
        ensureFocusedItemVisible();
        searchWindow_.show();
    }
    else {
        const auto itemCount = totalItemCount();
        focusedItemIndex_ = itemCount == 0
            ? 0U
            : std::min(previousFocus, itemCount - 1U);
        ensureFocusedItemVisible();
    }
    if (documentChangedHandler_) {
        documentChangedHandler_(document_);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::moveItemToTab(
    const std::size_t absoluteIndex,
    const std::size_t targetTabIndex)
{
    const auto source = itemLocationForDisplayedIndex(absoluteIndex);
    if (!source || targetTabIndex >= document_.tabs.size()
        || source->tabIndex == targetTabIndex) {
        return;
    }

    auto updatedDocument = document_;
    const auto location = core::moveItem(
        updatedDocument,
        *source,
        targetTabIndex,
        core::gridSlotExtent(updatedDocument.tabs[targetTabIndex]));
    if (!location || !core::validateItemsDocument(updatedDocument).empty()) {
        showTaskMessage(
            window_,
            L"HLaunch 移动条目",
            L"无法把条目移动到目标分类。",
            TaskDialogIcon::Error,
            L"数据未发生变化。");
        return;
    }

    document_ = std::move(updatedDocument);
    activeTabIndex_ = location->tabIndex;
    focusedItemIndex_ = core::gridSlotForItem(
        document_.tabs[location->tabIndex], location->itemIndex).value_or(0U);
    keyboardSelectionActive_ = false;
    pageOffset_ = 0;
    wheelDeltaRemainder_ = 0;
    searchVisible_ = false;
    searchWindow_.setQuery({});
    searchWindow_.hide();
    rebuildSearchIndex();
    ensureFocusedItemVisible();
    if (documentChangedHandler_) {
        documentChangedHandler_(document_);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::beginTabDrag(
    const std::size_t tabIndex,
    const POINT clientPoint)
{
    if (tabIndex >= document_.tabs.size() || isSearchFiltering()) {
        return;
    }
    tabDragSourceIndex_ = tabIndex;
    tabDragTargetIndex_.reset();
    tabDragInsertionIndex_.reset();
    tabDragInsertionXDip_.reset();
    tabDragStart_ = clientPoint;
    tabDragCurrentPoint_ = clientPoint;
    tabDragActive_ = false;
    SetCapture(window_);
}

void LauncherWindow::updateTabDrag(const POINT clientPoint)
{
    if (!tabDragSourceIndex_) {
        return;
    }
    if (!tabDragActive_) {
        const auto horizontalDistance = std::abs(clientPoint.x - tabDragStart_.x);
        const auto verticalDistance = std::abs(clientPoint.y - tabDragStart_.y);
        if (horizontalDistance < GetSystemMetrics(SM_CXDRAG)
            && verticalDistance < GetSystemMetrics(SM_CYDRAG)) {
            return;
        }
        tabDragActive_ = true;
    }
    tabDragCurrentPoint_ = clientPoint;

    RECT client{};
    GetClientRect(window_, &client);
    const float scale = 96.0F / static_cast<float>(dpi_);
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = static_cast<float>(client.right) * scale,
        .clientHeightDip = static_cast<float>(client.bottom) * scale,
            .itemCount = displayedTileCount(),
        }, metrics_);
    const auto placement = calculateTabInsertionTarget(
        layout,
        document_.tabs.size(),
        *tabDragSourceIndex_,
        static_cast<float>(clientPoint.x) * scale,
        static_cast<float>(clientPoint.y) * scale,
        firstVisibleTabIndex_);
    tabDragTargetIndex_ = placement
        ? std::optional<std::size_t>{placement->targetIndex}
        : std::nullopt;
    tabDragInsertionIndex_ = placement
        ? std::optional<std::size_t>{placement->insertionIndex}
        : std::nullopt;
    tabDragInsertionXDip_ = placement
        ? std::optional<float>{placement->xDip}
        : std::nullopt;
    updateDragPreviewWindow(calculateTabDragPreview(
        layout,
        document_.tabs.size(),
        static_cast<float>(clientPoint.x) * scale,
        static_cast<float>(clientPoint.y) * scale,
        firstVisibleTabIndex_));
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::finishTabDrag(const POINT clientPoint)
{
    updateTabDrag(clientPoint);
    const auto source = tabDragSourceIndex_;
    const auto target = tabDragTargetIndex_;
    cancelTabDrag();
    if (!source || !target || *source == *target) {
        return;
    }
    movePage(*source, *target);
}

void LauncherWindow::cancelTabDrag() noexcept
{
    hideDragPreviewWindow();
    tabDragSourceIndex_.reset();
    tabDragTargetIndex_.reset();
    tabDragInsertionIndex_.reset();
    tabDragInsertionXDip_.reset();
    tabDragActive_ = false;
    if (GetCapture() == window_) {
        ReleaseCapture();
    }
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void LauncherWindow::beginItemDrag(const std::size_t absoluteIndex, const POINT clientPoint)
{
    const auto source = itemLocationForDisplayedIndex(absoluteIndex);
    if (!source || isSearchFiltering()) {
        return;
    }
    itemDragSource_ = source;
    itemDropTarget_.reset();
    pressedItemIndex_ = absoluteIndex;
    itemDragStart_ = clientPoint;
    itemDragCurrentPoint_ = clientPoint;
    itemDragActive_ = false;
    SetCapture(window_);
}

void LauncherWindow::updateItemDrag(const POINT clientPoint)
{
    if (!itemDragSource_) {
        return;
    }
    itemDragCurrentPoint_ = clientPoint;
    if (!itemDragActive_) {
        const auto horizontalDistance = std::abs(clientPoint.x - itemDragStart_.x);
        const auto verticalDistance = std::abs(clientPoint.y - itemDragStart_.y);
        if (horizontalDistance < GetSystemMetrics(SM_CXDRAG)
            && verticalDistance < GetSystemMetrics(SM_CYDRAG)) {
            return;
        }
        itemDragActive_ = true;
    }

    RECT client{};
    GetClientRect(window_, &client);
    const float xDip = static_cast<float>(clientPoint.x) * 96.0F
        / static_cast<float>(dpi_);
    const float yDip = static_cast<float>(clientPoint.y) * 96.0F
        / static_cast<float>(dpi_);
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = static_cast<float>(client.right) * 96.0F
            / static_cast<float>(dpi_),
        .clientHeightDip = static_cast<float>(client.bottom) * 96.0F
            / static_cast<float>(dpi_),
            .itemCount = displayedTileCount(),
        }, metrics_);

    std::optional<InternalDropTarget> target{};
    if (const auto tabIndex = hitTestLauncherTab(
            layout,
            document_.tabs.size(),
            xDip,
            yDip,
            firstVisibleTabIndex_)) {
        const auto targetExtent = core::gridSlotExtent(document_.tabs[*tabIndex]);
        target = InternalDropTarget{
            .tabIndex = *tabIndex,
            .gridSlot = *tabIndex == itemDragSource_->tabIndex && targetExtent > 0U
                ? targetExtent - 1U
                : targetExtent,
            .tabTarget = true,
        };
    }
    else if (const auto displayedIndex = hitTestLauncherItem(layout, xDip, yDip)) {
        const auto requestedIndex = pageOffset_ + *displayedIndex;
        target = InternalDropTarget{
            .tabIndex = activeTabIndex_,
            .gridSlot = requestedIndex,
            .tabTarget = false,
            .displayedTileIndex = *displayedIndex,
        };
    }

    itemDropTarget_ = target;
    updateDragPreviewWindow(calculateItemDragPreview(layout, xDip, yDip));
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::finishItemDrag(const POINT clientPoint)
{
    updateItemDrag(clientPoint);
    const auto source = itemDragSource_;
    const auto target = itemDropTarget_;
    cancelItemDrag();
    if (!source || !target) {
        return;
    }
    const auto sourceGridSlot = core::gridSlotForItem(
        document_.tabs[source->tabIndex], source->itemIndex);
    if (source->tabIndex == target->tabIndex
        && sourceGridSlot == target->gridSlot) {
        return;
    }

    auto updatedDocument = document_;
    const auto location = core::moveItem(
        updatedDocument,
        *source,
        target->tabIndex,
        target->gridSlot);
    if (!location || !core::validateItemsDocument(updatedDocument).empty()) {
        return;
    }

    document_ = std::move(updatedDocument);
    activeTabIndex_ = location->tabIndex;
    focusedItemIndex_ = core::gridSlotForItem(
        document_.tabs[location->tabIndex], location->itemIndex).value_or(0U);
    pageOffset_ = 0;
    wheelDeltaRemainder_ = 0;
    rebuildSearchIndex();
    ensureFocusedItemVisible();
    if (documentChangedHandler_) {
        documentChangedHandler_(document_);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::cancelItemDrag() noexcept
{
    hideDragPreviewWindow();
    itemDragSource_.reset();
    itemDropTarget_.reset();
    pressedItemIndex_.reset();
    itemDragActive_ = false;
    if (GetCapture() == window_) {
        ReleaseCapture();
    }
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void LauncherWindow::submitDroppedSources(
    std::vector<platform::windows::DroppedSource> sources,
    const POINTL screenPoint)
{
    if (!dropResolver_ || sources.empty() || document_.tabs.empty()) {
        return;
    }

    std::size_t targetTabIndex = activeTabIndex_;
    std::optional<std::size_t> targetGridSlot{};
    std::optional<std::size_t> targetDisplayedIndex{};
    POINT clientPoint{screenPoint.x, screenPoint.y};
    if (ScreenToClient(window_, &clientPoint)) {
        RECT client{};
        GetClientRect(window_, &client);
        const auto layout = calculateLauncherLayout({
            .clientWidthDip = static_cast<float>(client.right) * 96.0F
                / static_cast<float>(dpi_),
            .clientHeightDip = static_cast<float>(client.bottom) * 96.0F
                / static_cast<float>(dpi_),
            .itemCount = displayedTileCount(),
        }, metrics_);
        const float xDip = static_cast<float>(clientPoint.x) * 96.0F
            / static_cast<float>(dpi_);
        const float yDip = static_cast<float>(clientPoint.y) * 96.0F
            / static_cast<float>(dpi_);
        if (const auto slot = hitTestLauncherItem(layout, xDip, yDip)) {
            targetDisplayedIndex = pageOffset_ + *slot;
            if (!isSearchFiltering()) {
                targetGridSlot = *targetDisplayedIndex;
            }
        }
        if (!isSearchFiltering()) {
            if (const auto tab = hitTestLauncherTab(
                    layout,
                    document_.tabs.size(),
                    xDip,
                    yDip,
                    firstVisibleTabIndex_)) {
                targetTabIndex = *tab;
            }
        }
    }

    const bool hasDroppedPath = std::ranges::any_of(
        sources,
        [](const platform::windows::DroppedSource& source) {
            return source.kind == platform::windows::DroppedSourceKind::Path;
        });
    if (hasDroppedPath && targetDisplayedIndex && launchHandler_) {
        if (const auto displayed = displayedItem(*targetDisplayedIndex);
            displayed && displayed->item
            && (displayed->item->type == core::ItemType::Application
                || displayed->item->type == core::ItemType::Shortcut)) {
            auto resolvedItem = resolveItemForUse(
                *displayed->item,
                L"将拖入文件作为参数启动该条目");
            if (!resolvedItem) {
                return;
            }
            auto launchItem = platform::windows::makeDropLaunchItem(
                std::move(*resolvedItem), sources);
            if (!launchItem) {
                showTaskMessage(
                    window_, L"HLaunch 拖放", L"无法读取拖入文件的路径。",
                    TaskDialogIcon::Error, L"本次未启动条目。");
                return;
            }
            launchHandler_(*launchItem);
            return;
        }
    }

    dropResolver_->submit({
        .targetTabIndex = targetTabIndex,
        .targetGridSlot = targetGridSlot,
        .sources = std::move(sources),
    });
}

void LauncherWindow::applyDropImport(platform::windows::DropImportResult result)
{
    if (result.failed) {
        showTaskMessage(
            window_, L"HLaunch 拖放", L"无法解析拖入内容。",
            TaskDialogIcon::Error, L"请检查文件或网址后重试。");
        return;
    }
    if (result.targetTabIndex >= document_.tabs.size()) {
        return;
    }
    if (result.items.empty()) {
        showTaskMessage(
            window_, L"HLaunch 拖放", L"拖入内容中没有可添加的项目。",
            TaskDialogIcon::Information,
            L"支持文件、文件夹、快捷方式和网址。");
        return;
    }

    for (auto& item : result.items) {
        auto portableItem = platform::windows::makeItemPathsPortable(item, itemPathContext_);
        if (!portableItem) {
            showTaskMessage(
                window_, L"HLaunch 拖放", L"无法保存拖入条目的路径。",
                TaskDialogIcon::Warning, L"本次没有添加条目。");
            return;
        }
        item = std::move(*portableItem);
    }

    auto preview = document_;
    std::size_t duplicateCount{};
    for (const auto& item : result.items) {
        if (core::hasExactLaunchDuplicate(preview, item)) {
            ++duplicateCount;
        }
        else {
            preview.tabs[result.targetTabIndex].items.push_back(item);
        }
    }
    const bool allowDuplicates = duplicateCount > 0
        && confirmTask(
            window_, L"HLaunch 重复条目",
            L"发现 " + std::to_wstring(duplicateCount)
                + L" 个启动属性完全相同的条目。是否仍然添加？",
            L"选择“否”将跳过这些条目。",
            TaskDialogIcon::Warning);

    for (auto& item : result.items) {
        auto id = platform::windows::createUuidV4();
        if (!id) {
            showTaskMessage(
                window_, L"HLaunch 拖放", L"无法生成条目标识。",
                TaskDialogIcon::Error);
            return;
        }
        item.id = std::move(*id);
    }

    auto updatedDocument = document_;
    const auto mutation = core::addImportedItems(
        updatedDocument,
        result.targetTabIndex,
        std::move(result.items),
        allowDuplicates,
        result.targetGridSlot);
    if (!mutation || !core::validateItemsDocument(updatedDocument).empty()) {
        showTaskMessage(
            window_, L"HLaunch 拖放", L"本次没有添加条目。",
            TaskDialogIcon::Warning,
            L"拖入条目超过数据限制或未通过校验。");
        return;
    }

    if (!mutation->added.empty()) {
        document_ = std::move(updatedDocument);
        activeTabIndex_ = result.targetTabIndex;
        focusedItemIndex_ = core::gridSlotForItem(
            document_.tabs[result.targetTabIndex],
            mutation->added.front().itemIndex).value_or(0U);
        pageOffset_ = 0;
        searchVisible_ = false;
        searchWindow_.setQuery({});
        searchWindow_.hide();
        rebuildSearchIndex();
        ensureFocusedItemVisible();
        if (documentChangedHandler_) {
            documentChangedHandler_(document_);
        }
        InvalidateRect(window_, nullptr, FALSE);
    }

    if (mutation->skippedDuplicates > 0 || result.unsupportedCount > 0) {
        std::wstring summary = L"已添加 " + std::to_wstring(mutation->added.size()) + L" 个条目。";
        if (mutation->skippedDuplicates > 0) {
            summary += L"\n跳过重复条目：" + std::to_wstring(mutation->skippedDuplicates) + L" 个。";
        }
        if (result.unsupportedCount > 0) {
            summary += L"\n无法识别：" + std::to_wstring(result.unsupportedCount) + L" 个。";
        }
        showTaskMessage(
            window_, L"HLaunch 拖放", L"拖放导入完成。",
            TaskDialogIcon::Information, summary);
    }
}

void LauncherWindow::shutdownDropServices() noexcept
{
    dropTarget_.revoke();
    dropResolver_.reset();
    if (!window_) {
        return;
    }
    MSG pending{};
    while (PeekMessageW(
        &pending,
        window_,
        dropImportCompletedMessage,
        dropImportCompletedMessage,
        PM_REMOVE)) {
        delete reinterpret_cast<platform::windows::DropImportResult*>(pending.lParam); // NOLINT(performance-no-int-to-ptr): This message owns the heap result.
    }
}

void LauncherWindow::applyIconLoadResult(platform::windows::IconLoadResult result)
{
    const auto cached = iconCache_.find(IconCacheKey{
        .itemId = result.itemId,
        .pixelSize = result.requestedPixelSize,
    });
    if (cached == iconCache_.end()
        || cached->second.sourceKey != result.sourceKey) {
        return;
    }

    auto& entry = cached->second;
    entry.pending = false;
    entry.failed = !result.succeeded;
    entry.width = result.width;
    entry.height = result.height;
    entry.pixels = std::move(result.pixels);
    entry.bitmap = nullptr;
    InvalidateRect(window_, nullptr, FALSE);
    if (itemDragActive_ && itemDragSource_
        && itemDragSource_->tabIndex < document_.tabs.size()
        && itemDragSource_->itemIndex
            < document_.tabs[itemDragSource_->tabIndex].items.size()
        && document_.tabs[itemDragSource_->tabIndex]
                .items[itemDragSource_->itemIndex].id == result.itemId) {
        InvalidateRect(dragPreviewWindow_, nullptr, FALSE);
    }
}

void LauncherWindow::shutdownIconServices() noexcept
{
    iconLoader_.reset();
    if (!window_) {
        return;
    }
    MSG pending{};
    while (PeekMessageW(
        &pending,
        window_,
        iconLoadCompletedMessage,
        iconLoadCompletedMessage,
        PM_REMOVE)) {
        delete reinterpret_cast<platform::windows::IconLoadResult*>(pending.lParam); // NOLINT(performance-no-int-to-ptr): This message owns the heap result.
    }
}

void LauncherWindow::rebuildSearchIndex()
{
    searchIndex_.rebuild(document_, [](const std::string_view name) {
        return platform::windows::normalizeSearchText(name);
    });
    if (accessibility_) accessibility_->raiseStructureChanged();
}

std::vector<LauncherAccessibleNode> LauncherWindow::accessibilitySnapshot() const
{
    if (!window_) return {};
    RECT client{};
    RECT screen{};
    if (!GetClientRect(window_, &client) || !GetWindowRect(window_, &screen)) return {};
    const float scale = static_cast<float>(dpi_) / 96.0F;
    const float widthDip = static_cast<float>(client.right) / scale;
    const float heightDip = static_cast<float>(client.bottom) / scale;
    const auto layout = calculateLauncherLayout(
        {widthDip, heightDip, displayedTileCount()}, metrics_);
    const auto bounds = [&](const RectDip& rectangle) {
        return UiaRect{
            static_cast<double>(screen.left) + static_cast<double>(rectangle.x * scale),
            static_cast<double>(screen.top) + static_cast<double>(rectangle.y * scale),
            static_cast<double>(rectangle.width * scale),
            static_cast<double>(rectangle.height * scale),
        };
    };
    const bool hidden = !IsWindowVisible(window_);
    const bool windowHasFocus = GetFocus() == window_ || IsChild(window_, GetFocus());
    std::vector<LauncherAccessibleNode> nodes{};
    nodes.reserve(6U + document_.tabs.size() + visibleItemCount());
    nodes.push_back({
        .key = std::wstring{accessibleRootKey}, .name = L"HLaunch",
        .controlType = UIA_WindowControlTypeId,
        .bounds = UiaRect{static_cast<double>(screen.left), static_cast<double>(screen.top),
                          static_cast<double>(screen.right - screen.left),
                          static_cast<double>(screen.bottom - screen.top)},
        .keyboardFocusable = true,
        .hasKeyboardFocus = windowHasFocus && accessibilityFocusKey_ == accessibleRootKey,
        .offscreen = hidden,
    });
    const auto addButton = [&](const std::wstring_view key, const std::wstring_view name, const RectDip& rect) {
        nodes.push_back({
            .key = std::wstring{key}, .parentKey = std::wstring{accessibleRootKey},
            .name = std::wstring{name}, .controlType = UIA_ButtonControlTypeId,
            .bounds = bounds(rect), .keyboardFocusable = true,
            .hasKeyboardFocus = windowHasFocus && accessibilityFocusKey_ == key,
            .offscreen = hidden, .invokable = true,
        });
    };
    addButton(accessibleMenuKey, L"主菜单", layout.menuButton);
    addButton(
        accessiblePinKey,
        windowPinned_ ? L"取消置顶窗口" : L"置顶窗口",
        layout.pinButton);
    addButton(accessibleCloseKey, L"隐藏 HLaunch", layout.closeButton);
    nodes.push_back({
        .key = std::wstring{accessibleTabsKey}, .parentKey = std::wstring{accessibleRootKey},
        .name = L"分类", .controlType = UIA_TabControlTypeId, .bounds = bounds(layout.tabs),
        .offscreen = hidden, .selectionContainer = true,
    });
    const auto tabViewport = calculateLauncherTabViewport(
        layout, document_.tabs.size(), firstVisibleTabIndex_);
    if (!document_.tabs.empty()) {
        for (std::size_t index = 0; index < document_.tabs.size(); ++index) {
            const auto& tab = document_.tabs[index];
            const auto tabBounds = calculateLauncherTabRect(
                layout, document_.tabs.size(), index, tabViewport.firstIndex);
            const std::wstring key = L"tab:" + utf8ToWide(tab.id);
            nodes.push_back({
                .key = key, .parentKey = std::wstring{accessibleTabsKey},
                .name = utf8ToWide(tab.name), .controlType = UIA_TabItemControlTypeId,
                .bounds = bounds(tabBounds),
                .keyboardFocusable = true,
                .hasKeyboardFocus = windowHasFocus && accessibilityFocusKey_ == key,
                .offscreen = hidden || !tabViewport.contains(index),
                .selectable = true, .selected = index == activeTabIndex_,
                .positionInSet = static_cast<int>(index + 1U),
                .sizeOfSet = static_cast<int>(document_.tabs.size()),
            });
        }
    }
    nodes.push_back({
        .key = std::wstring{accessibleItemsKey}, .parentKey = std::wstring{accessibleRootKey},
        .name = L"启动项目", .controlType = UIA_ListControlTypeId, .bounds = bounds(layout.grid),
        .offscreen = hidden, .selectionContainer = true,
    });
    const auto visible = visibleItemCount();
    const auto total = totalItemCount();
    for (std::size_t visibleIndex = 0; visibleIndex < visible && visibleIndex < layout.items.size(); ++visibleIndex) {
        const auto absoluteIndex = pageOffset_ + visibleIndex;
        const auto displayed = displayedItem(absoluteIndex);
        if (!displayed || !displayed->item) continue;
        const std::wstring key = L"item:" + utf8ToWide(displayed->item->id);
        nodes.push_back({
            .key = key, .parentKey = std::wstring{accessibleItemsKey},
            .name = utf8ToWide(displayed->item->name), .controlType = UIA_ListItemControlTypeId,
            .bounds = bounds(layout.items[visibleIndex]), .keyboardFocusable = true,
            .hasKeyboardFocus = windowHasFocus && accessibilityFocusKey_ == key,
            .enabled = true, .offscreen = hidden, .invokable = true, .selectable = true,
            .selected = keyboardSelectionActive_ && focusedItemIndex_ == absoluteIndex,
            .positionInSet = static_cast<int>(absoluteIndex + 1U),
            .sizeOfSet = static_cast<int>(total),
        });
    }
    return nodes;
}

void LauncherWindow::focusAccessibleNode(const std::wstring_view key)
{
    if (!window_) return;
    SetFocus(window_);
    accessibilityFocusKey_ = key;
    if (key.starts_with(L"tab:")) {
        const auto id = key.substr(4U);
        for (std::size_t index = 0; index < document_.tabs.size(); ++index) {
            if (utf8ToWide(document_.tabs[index].id) == id) {
                changeActiveTab(index);
                break;
            }
        }
    }
    else if (key.starts_with(L"item:")) {
        const auto id = key.substr(5U);
        for (std::size_t index = 0; index < totalItemCount(); ++index) {
            const auto displayed = displayedItem(index);
            if (displayed && displayed->item && utf8ToWide(displayed->item->id) == id) {
                focusedItemIndex_ = index;
                keyboardSelectionActive_ = true;
                ensureFocusedItemVisible();
                break;
            }
        }
    }
    InvalidateRect(window_, nullptr, FALSE);
    if (accessibility_) {
        accessibility_->raiseFocusChanged(key);
        if (key.starts_with(L"tab:") || key.starts_with(L"item:")) {
            accessibility_->raiseSelectionChanged(key);
        }
    }
}

void LauncherWindow::invokeAccessibleNode(const std::wstring_view key)
{
    if (key == accessibleMenuKey) {
        const auto nodes = accessibilitySnapshot();
        const auto found = std::ranges::find(nodes, std::wstring{accessibleMenuKey}, &LauncherAccessibleNode::key);
        POINT point{};
        if (found != nodes.end()) {
            point = POINT{static_cast<LONG>(found->bounds.left),
                          static_cast<LONG>(found->bounds.top + found->bounds.height)};
        }
        showLauncherContextMenu(point);
        return;
    }
    if (key == accessiblePinKey) { toggleWindowPin(); return; }
    if (key == accessibleCloseKey) { hide(); return; }
    focusAccessibleNode(key);
    if (key.starts_with(L"item:")) activateFocusedItem();
}

void LauncherWindow::activateFocusedItem()
{
    if (const auto displayed = displayedItem(focusedItemIndex_);
        displayed && displayed->item && launchHandler_) {
        if (const auto item = resolveItemForUse(*displayed->item, L"启动该条目")) {
            launchHandler_(*item);
        }
    }
}

void LauncherWindow::recordSuccessfulLaunch(const std::string_view itemId)
{
    if (itemId.empty()) {
        return;
    }
    for (auto& tab : document_.tabs) {
        const auto found = std::ranges::find(tab.items, itemId, &core::LaunchItem::id);
        if (found == tab.items.end()) {
            continue;
        }
        if (found->launchCount < std::numeric_limits<std::uint64_t>::max()) {
            ++found->launchCount;
        }
        if (const auto timestamp = currentUtcTimestamp(); !timestamp.empty()) {
            found->lastLaunchedAt = timestamp;
        }
        rebuildSearchIndex();
        if (isSearchFiltering()) {
            updateSearch(searchWindow_.query());
        }
        if (documentChangedHandler_) {
            documentChangedHandler_(document_);
        }
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
}

std::optional<core::LaunchItem> LauncherWindow::resolveItemForUse(
    const core::LaunchItem& item,
    const std::wstring_view action)
{
    const auto resolved = platform::windows::resolveItemPaths(item, itemPathContext_);
    if (resolved) {
        return *resolved;
    }
    std::wstring detail = L"请检查目标、工作目录和图标路径。";
    if (resolved.error().systemCode != 0U) {
        detail += L"\n\n系统错误码：" + std::to_wstring(resolved.error().systemCode);
    }
    showTaskMessage(
        window_, L"HLaunch 条目路径", L"无法" + std::wstring{action} + L"。",
        TaskDialogIcon::Error, detail);
    return std::nullopt;
}

void LauncherWindow::changeActiveTab(const std::size_t tabIndex)
{
    if (tabIndex >= document_.tabs.size()) {
        return;
    }
    clearHover();
    activeTabIndex_ = tabIndex;
    ensureActiveTabVisible();
    focusedItemIndex_ = 0;
    keyboardSelectionActive_ = false;
    pageOffset_ = 0;
    wheelDeltaRemainder_ = 0;
    accessibilityFocusKey_ = L"tab:" + utf8ToWide(document_.tabs[tabIndex].id);
    if (accessibility_) accessibility_->raiseSelectionChanged(accessibilityFocusKey_);
    InvalidateRect(window_, nullptr, FALSE);
}

const core::Tab* LauncherWindow::activeTab() const noexcept
{
    if (activeTabIndex_ >= document_.tabs.size()) {
        return nullptr;
    }
    return &document_.tabs[activeTabIndex_];
}

std::optional<core::ItemLocation> LauncherWindow::itemLocationForDisplayedIndex(
    const std::size_t index) const noexcept
{
    if (isSearchFiltering()) {
        if (index >= searchResults_.size()) {
            return std::nullopt;
        }
        return core::ItemLocation{
            searchResults_[index].tabIndex,
            searchResults_[index].itemIndex,
        };
    }
    const auto* tab = activeTab();
    if (!tab) {
        return std::nullopt;
    }
    const auto itemIndex = core::itemIndexAtGridSlot(*tab, index);
    if (!itemIndex) {
        return std::nullopt;
    }
    return core::ItemLocation{activeTabIndex_, *itemIndex};
}

std::optional<LauncherWindow::DisplayedItem> LauncherWindow::displayedItem(
    const std::size_t index) const noexcept
{
    if (isSearchFiltering()) {
        if (index >= searchResults_.size()) {
            return std::nullopt;
        }
        const auto& result = searchResults_[index];
        if (result.tabIndex >= document_.tabs.size()) {
            return std::nullopt;
        }
        const auto& tab = document_.tabs[result.tabIndex];
        if (result.itemIndex >= tab.items.size()) {
            return std::nullopt;
        }
        return DisplayedItem{&tab.items[result.itemIndex]};
    }

    const auto* tab = activeTab();
    if (!tab) {
        return std::nullopt;
    }
    const auto itemIndex = core::itemIndexAtGridSlot(*tab, index);
    if (!itemIndex) {
        return std::nullopt;
    }
    return DisplayedItem{&tab->items[*itemIndex]};
}

bool LauncherWindow::isSearchFiltering() const noexcept
{
    return searchVisible_ && !searchWindow_.query().empty();
}

std::size_t LauncherWindow::totalItemCount() const noexcept
{
    if (isSearchFiltering()) {
        return searchResults_.size();
    }
    const auto* tab = activeTab();
    return tab ? core::gridSlotExtent(*tab) : 0U;
}

std::optional<std::size_t> LauncherWindow::nearestDisplayedItemIndex(
    const std::size_t preferred,
    const bool preferForward) const noexcept
{
    const auto itemCount = totalItemCount();
    if (itemCount == 0U) {
        return std::nullopt;
    }
    const auto start = std::min(preferred, itemCount - 1U);
    if (displayedItem(start)) {
        return start;
    }
    for (std::size_t distance = 1U; distance < itemCount; ++distance) {
        const auto forward = start + distance;
        const auto backward = start >= distance
            ? std::optional<std::size_t>{start - distance}
            : std::nullopt;
        const auto tryForward = [&]() -> std::optional<std::size_t> {
            return forward < itemCount && displayedItem(forward)
                ? std::optional<std::size_t>{forward}
                : std::nullopt;
        };
        const auto tryBackward = [&]() -> std::optional<std::size_t> {
            return backward && displayedItem(*backward) ? backward : std::nullopt;
        };
        if (preferForward) {
            if (const auto match = tryForward()) return match;
            if (const auto match = tryBackward()) return match;
        }
        else {
            if (const auto match = tryBackward()) return match;
            if (const auto match = tryForward()) return match;
        }
    }
    return std::nullopt;
}

std::size_t LauncherWindow::pageCapacity() const
{
    if (!window_) {
        return defaultVisibleItems;
    }
    RECT client{};
    if (!GetClientRect(window_, &client)) {
        return defaultVisibleItems;
    }
    const float widthDip = static_cast<float>(client.right - client.left) * 96.0F
        / static_cast<float>(dpi_);
    const float heightDip = static_cast<float>(client.bottom - client.top) * 96.0F
        / static_cast<float>(dpi_);
    const auto availableCapacity = calculateLauncherGridCapacity(widthDip, heightDip, metrics_);
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = widthDip,
        .clientHeightDip = heightDip,
        .itemCount = 0,
    }, metrics_);
    return availableCapacity >= layout.columns
        ? (availableCapacity / layout.columns) * layout.columns
        : availableCapacity;
}

std::size_t LauncherWindow::maximumPageOffset() const
{
    const auto itemCount = totalItemCount();
    const auto capacity = pageCapacity();
    if (capacity == 0 || itemCount <= capacity) {
        return 0;
    }

    RECT client{};
    GetClientRect(window_, &client);
    const float widthDip = static_cast<float>(client.right - client.left) * 96.0F
        / static_cast<float>(dpi_);
    const float heightDip = static_cast<float>(client.bottom - client.top) * 96.0F
        / static_cast<float>(dpi_);
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = widthDip,
        .clientHeightDip = heightDip,
        .itemCount = 0,
    }, metrics_);
    const auto rowSize = std::min(layout.columns, capacity);
    const auto overflow = itemCount - capacity;
    return ((overflow + rowSize - 1U) / rowSize) * rowSize;
}

void LauncherWindow::ensureFocusedItemVisible()
{
    const auto itemCount = totalItemCount();
    const auto capacity = pageCapacity();
    if (itemCount == 0 || capacity == 0) {
        focusedItemIndex_ = 0;
        pageOffset_ = 0;
        return;
    }
    const auto occupiedFocus = nearestDisplayedItemIndex(focusedItemIndex_);
    if (!occupiedFocus) {
        focusedItemIndex_ = 0;
        pageOffset_ = 0;
        return;
    }
    focusedItemIndex_ = *occupiedFocus;
    pageOffset_ = std::min(pageOffset_, maximumPageOffset());

    RECT client{};
    GetClientRect(window_, &client);
    const float widthDip = static_cast<float>(client.right - client.left) * 96.0F
        / static_cast<float>(dpi_);
    const float heightDip = static_cast<float>(client.bottom - client.top) * 96.0F
        / static_cast<float>(dpi_);
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = widthDip,
        .clientHeightDip = heightDip,
        .itemCount = 0,
    }, metrics_);
    const auto rowSize = std::min(layout.columns, capacity);
    if (focusedItemIndex_ < pageOffset_) {
        pageOffset_ = (focusedItemIndex_ / rowSize) * rowSize;
    }
    else if (focusedItemIndex_ >= pageOffset_ + capacity) {
        const auto requestedOffset = focusedItemIndex_ - capacity + 1U;
        pageOffset_ = ((requestedOffset + rowSize - 1U) / rowSize) * rowSize;
    }
    pageOffset_ = std::min(pageOffset_, maximumPageOffset());
}

std::size_t LauncherWindow::visibleItemCount() const
{
    const auto itemCount = totalItemCount();
    const auto capacity = pageCapacity();
    if (capacity == 0 || pageOffset_ >= itemCount) {
        return 0;
    }
    return std::min(capacity, itemCount - pageOffset_);
}

std::size_t LauncherWindow::displayedTileCount() const
{
    if (!activeTab() && !isSearchFiltering()) {
        return 0;
    }
    return pageCapacity();
}

LauncherWindow::CachedItemIcon* LauncherWindow::ensureItemIcon(
    const core::LaunchItem& item,
    const std::uint32_t dpi)
{
    const auto resolvedItem = platform::windows::resolveItemPaths(item, itemPathContext_);
    const auto sourceKey = resolvedItem
        ? iconSourceKey(*resolvedItem)
        : iconSourceKey(item) + "|invalid-path";
    const auto requestedPixelSize = iconPixelSizeForDpi(metrics_.itemIconSize, dpi);

    for (auto existing = iconCache_.begin(); existing != iconCache_.end();) {
        if (existing->first.itemId == item.id
            && existing->second.sourceKey != sourceKey) {
            existing = iconCache_.erase(existing);
        }
        else {
            ++existing;
        }
    }

    IconCacheKey cacheKey{
        .itemId = item.id,
        .pixelSize = requestedPixelSize,
    };
    auto cached = iconCache_.find(cacheKey);
    if (cached == iconCache_.end()) {
        if (iconCache_.size() >= maximumIconCacheEntries) {
            auto oldest = iconCache_.begin();
            for (auto candidate = std::next(iconCache_.begin());
                 candidate != iconCache_.end(); ++candidate) {
                if (candidate->second.lastUsed < oldest->second.lastUsed) {
                    oldest = candidate;
                }
            }
            if (oldest != iconCache_.end()) {
                iconCache_.erase(oldest);
            }
        }
        CachedItemIcon replacement{
            .sourceKey = sourceKey,
            .requestedPixelSize = requestedPixelSize,
            .lastUsed = ++iconCacheUseSequence_,
            .pending = iconLoader_ != nullptr && resolvedItem.has_value(),
            .failed = iconLoader_ == nullptr || !resolvedItem.has_value(),
        };
        cached = iconCache_.emplace(std::move(cacheKey), std::move(replacement)).first;
        if (iconLoader_ && resolvedItem) {
            iconLoader_->submit({
                .itemId = item.id,
                .sourceKey = sourceKey,
                .target = resolvedItem->target,
                .icon = resolvedItem->icon,
                .pixelSize = requestedPixelSize,
            });
        }
    }

    auto& entry = cached->second;
    entry.lastUsed = ++iconCacheUseSequence_;
    return &entry;
}

ID2D1Bitmap* LauncherWindow::itemIconBitmap(const core::LaunchItem& item)
{
    auto* entry = ensureItemIcon(item, dpi_);
    if (!entry) {
        return nullptr;
    }
    if (entry->pending || entry->failed || entry->pixels.empty() || !renderTarget_) {
        return nullptr;
    }
    if (!entry->bitmap) {
        const auto properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi_),
            static_cast<float>(dpi_));
        if (FAILED(renderTarget_->CreateBitmap(
                D2D1::SizeU(entry->width, entry->height),
                entry->pixels.data(),
                entry->width * 4U,
                properties,
                entry->bitmap.put()))) {
            return nullptr;
        }
    }
    return entry->bitmap.get();
}

void LauncherWindow::drawText(
    const std::wstring_view text,
    const D2D1_RECT_F& bounds,
    IDWriteTextFormat* const format,
    ID2D1Brush* const brush)
{
    renderTarget_->DrawTextW(
        text.data(),
        static_cast<UINT32>(text.size()),
        format,
        bounds,
        brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP,
        DWRITE_MEASURING_MODE_NATURAL);
}

} // namespace hlaunch::ui
