#include "ui/launcher_window.h"

#include "core/item_operations.h"
#include "core/data_validation.h"
#include "platform/windows/search_text.h"
#include "platform/windows/uuid.h"
#include "ui/item_context_menu.h"
#include "ui/item_editor_dialog.h"
#include "ui/launcher_layout.h"

#include <d2d1helper.h>
#include <windowsx.h>
#include <wil/resource.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr wchar_t launcherWindowClass[] = L"HLaunch.LauncherWindow.v1";
constexpr float cornerRadius = 12.0F;
constexpr int launcherWidthDip = 420;
constexpr int launcherHeightDip = 640;

constexpr std::size_t maximumVisibleItems = 25;
constexpr std::size_t maximumIconCacheEntries = 128;
constexpr UINT dropImportCompletedMessage = WM_APP + 0x43U;
constexpr UINT iconLoadCompletedMessage = WM_APP + 0x44U;
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

} // namespace

LauncherWindow::~LauncherWindow()
{
    shutdownIconServices();
    shutdownDropServices();
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
    DocumentChangedHandler documentChangedHandler)
{
    document_ = std::move(document);
    rebuildSearchIndex();
    launchHandler_ = std::move(launchHandler);
    documentChangedHandler_ = std::move(documentChangedHandler);
    activeTabIndex_ = 0;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    windowClass.lpfnWndProc = &LauncherWindow::windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = launcherWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    dpi_ = GetDpiForSystem();
    const auto width = MulDiv(launcherWidthDip, static_cast<int>(dpi_), 96);
    const auto height = MulDiv(launcherHeightDip, static_cast<int>(dpi_), 96);
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const auto x = workArea.left + ((workArea.right - workArea.left - width) / 2);
    const auto y = workArea.top + ((workArea.bottom - workArea.top - height) / 2);
    window_ = CreateWindowExW(
        WS_EX_APPWINDOW,
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

    translucentSurface_ = effects.backdrop != platform::windows::WindowBackdrop::Solid;
    static_cast<void>(platform::windows::applyWindowEffects(window_, effects));
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
            });
    }
    catch (...) {
        iconLoader_.reset();
    }
    searchVisible_ = showSearch;
    return true;
}

void LauncherWindow::setDocumentChangedHandler(DocumentChangedHandler handler)
{
    documentChangedHandler_ = std::move(handler);
}

void LauncherWindow::setDeleteConfirmationHandler(DeleteConfirmationHandler handler)
{
    deleteConfirmationHandler_ = std::move(handler);
}

void LauncherWindow::setItemEditorHandler(ItemEditorHandler handler)
{
    itemEditorHandler_ = std::move(handler);
}

void LauncherWindow::show()
{
    positionOnCursorMonitor();
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

    const int desiredWidth = MulDiv(launcherWidthDip, static_cast<int>(dpi_), 96);
    const int desiredHeight = MulDiv(launcherHeightDip, static_cast<int>(dpi_), 96);
    const auto placement = calculateCenteredWindowRectangle(
        RectPixels{info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom},
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

    const int desiredWidth = MulDiv(launcherWidthDip, static_cast<int>(dpi_), 96);
    const int desiredHeight = MulDiv(launcherHeightDip, static_cast<int>(dpi_), 96);
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

void LauncherWindow::hide()
{
    cancelItemDrag();
    searchVisible_ = false;
    searchResults_.clear();
    pageOffset_ = 0;
    wheelDeltaRemainder_ = 0;
    focusedItemIndex_ = 0;
    searchWindow_.setQuery({});
    searchWindow_.hide();
    ShowWindow(window_, SW_HIDE);
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
    const LPARAM lParam)
{
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
        return self->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT LauncherWindow::handleMessage(
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
    case WM_SETFOCUS:
        windowFocused_ = true;
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        windowFocused_ = false;
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        return handleKeyDown(wParam)
            ? 0
            : DefWindowProcW(window_, message, wParam, lParam);
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
        if (isSearchFiltering()) {
            return 0;
        }
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
        });
        if (const auto itemIndex = hitTestLauncherItem(layout, xDip, yDip);
            itemIndex && *itemIndex < visibleItemCount()) {
            beginItemDrag(
                pageOffset_ + *itemIndex,
                POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            SetFocus(window_);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (itemDragSource_ && (wParam & MK_LBUTTON) != 0) {
            updateItemDrag(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window_, &point);
        RECT client{};
        GetClientRect(window_, &client);
        if (point.x < client.left || point.x >= client.right
            || point.y < client.top || point.y >= client.bottom) {
            return DefWindowProcW(window_, message, wParam, lParam);
        }
        const float xDip = static_cast<float>(point.x) * 96.0F / static_cast<float>(dpi_);
        const float yDip = static_cast<float>(point.y) * 96.0F / static_cast<float>(dpi_);
        const float widthDip = static_cast<float>(client.right) * 96.0F / static_cast<float>(dpi_);
        const float heightDip = static_cast<float>(client.bottom) * 96.0F / static_cast<float>(dpi_);
        const auto layout = calculateLauncherLayout({
            widthDip,
            heightDip,
            displayedTileCount(),
        });
        return isLauncherDragRegion(layout, xDip, yDip) ? HTCAPTION : HTCLIENT;
    }
    case WM_LBUTTONUP: {
        std::optional<std::size_t> requiredClickIndex{};
        const POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
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
        });
        const auto& closeButton = layout.closeButton;
        if (xDip >= closeButton.x && xDip < closeButton.x + closeButton.width
            && yDip >= closeButton.y && yDip < closeButton.y + closeButton.height) {
            DestroyWindow(window_);
            return 0;
        }
        if (!isSearchFiltering()) {
            if (const auto tabIndex = hitTestLauncherTab(
                    layout,
                    document_.tabs.size(),
                    xDip,
                    yDip)) {
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
                SetFocus(window_);
                InvalidateRect(window_, nullptr, FALSE);
                launchHandler_(*displayed->item);
            }
            else if (!isSearchFiltering() && *itemIndex >= visibleItemCount()) {
                showAddEditor();
            }
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
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
        });
        if (const auto itemIndex = hitTestLauncherItem(layout, xDip, yDip);
            itemIndex && *itemIndex < visibleItemCount()) {
            POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(window_, &screenPoint);
            showItemContextMenu(pageOffset_ + *itemIndex, screenPoint);
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
    case WM_SIZE:
        if (renderTarget_ && wParam != SIZE_MINIMIZED) {
            renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        }
        if (wParam != SIZE_MINIMIZED) {
            ensureFocusedItemVisible();
            positionSearchWindow();
        }
        return 0;
    case WM_MOVE:
        positionSearchWindow();
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        const auto* suggested = reinterpret_cast<const RECT*>(lParam); // NOLINT(performance-no-int-to-ptr): WM_DPICHANGED defines LPARAM as RECT*.
        SetWindowPos(
            window_,
            nullptr,
            suggested->left,
            suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        if (renderTarget_) {
            renderTarget_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        }
        positionSearchWindow();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* information = reinterpret_cast<MINMAXINFO*>(lParam); // NOLINT(performance-no-int-to-ptr): WM_GETMINMAXINFO defines LPARAM as MINMAXINFO*.
        information->ptMinTrackSize.x = MulDiv(320, static_cast<int>(dpi_), 96);
        information->ptMinTrackSize.y = MulDiv(520, static_cast<int>(dpi_), 96);
        return 0;
    }
    case WM_CLOSE:
        cancelItemDrag();
        shutdownIconServices();
        shutdownDropServices();
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        cancelItemDrag();
        shutdownIconServices();
        shutdownDropServices();
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
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

    constexpr wchar_t fontFamily[] = L"Segoe UI Variable Text";
    if (FAILED(writeFactory_->CreateTextFormat(
            fontFamily,
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            16.0F,
            L"zh-CN",
            titleFormat_.put()))) {
        return false;
    }
    if (FAILED(writeFactory_->CreateTextFormat(
            fontFamily,
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
            fontFamily,
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
            fontFamily,
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            10.0F,
            L"zh-CN",
            captionFormat_.put()))) {
        return false;
    }
    if (FAILED(writeFactory_->CreateTextFormat(
            fontFamily,
            nullptr,
            DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            13.0F,
            L"zh-CN",
            tabFormat_.put()))) {
        return false;
    }
    if (FAILED(writeFactory_->CreateTextFormat(
            fontFamily,
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            22.0F,
            L"zh-CN",
            iconFormat_.put()))) {
        return false;
    }
    titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    smallFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    captionFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    captionFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    tabFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    tabFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    iconFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    iconFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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

    const struct BrushDefinition {
        std::uint32_t color;
        float opacity;
        winrt::com_ptr<ID2D1SolidColorBrush>* destination;
    } brushes[]{
        {0x0B1120, translucentSurface_ ? 0.64F : 1.0F, &backgroundBrush_},
        {0x121B2D, translucentSurface_ ? 0.78F : 1.0F, &surfaceBrush_},
        {0x18243A, translucentSurface_ ? 0.84F : 1.0F, &elevatedBrush_},
        {0x2DD4BF, 1.0F, &accentBrush_},
        {0xF8FAFC, 1.0F, &textBrush_},
        {0x94A3B8, 1.0F, &mutedTextBrush_},
        {0x52627D, translucentSurface_ ? 0.58F : 1.0F, &borderBrush_},
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
    accentBrush_ = nullptr;
    elevatedBrush_ = nullptr;
    surfaceBrush_ = nullptr;
    backgroundBrush_ = nullptr;
    renderTarget_ = nullptr;
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
    });
    searchWindow_.positionAttached(
        window_,
        dpi_,
        calculateSearchPopupLayout(launcherLayout));
}

void LauncherWindow::render()
{
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (!createDeviceResources()) {
        EndPaint(window_, &paint);
        return;
    }

    const auto renderSize = renderTarget_->GetSize();
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = renderSize.width,
        .clientHeightDip = renderSize.height,
        .itemCount = displayedTileCount(),
    });

    renderTarget_->BeginDraw();
    renderTarget_->Clear(D2D1::ColorF(
        0x0B1120,
        translucentSurface_ ? 0.64F : 1.0F));

    const auto header = toD2dRect(layout.header);
    renderTarget_->FillRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(header.left, header.top + 5.0F, header.left + 18.0F, header.top + 23.0F),
            5.0F,
            5.0F),
        accentBrush_.get());
    drawText(
        L"HLaunch",
        D2D1::RectF(header.left + 28.0F, header.top, header.right - 44.0F, header.bottom),
        titleFormat_.get(),
        textBrush_.get());
    const auto closeButton = toD2dRect(layout.closeButton);
    const auto closeCenter = D2D1::Point2F(
        (closeButton.left + closeButton.right) / 2.0F,
        (closeButton.top + closeButton.bottom) / 2.0F);
    renderTarget_->DrawLine(
        D2D1::Point2F(closeCenter.x - 5.0F, closeCenter.y - 5.0F),
        D2D1::Point2F(closeCenter.x + 5.0F, closeCenter.y + 5.0F),
        mutedTextBrush_.get(),
        1.5F);
    renderTarget_->DrawLine(
        D2D1::Point2F(closeCenter.x + 5.0F, closeCenter.y - 5.0F),
        D2D1::Point2F(closeCenter.x - 5.0F, closeCenter.y + 5.0F),
        mutedTextBrush_.get(),
        1.5F);

    const bool filtering = isSearchFiltering();
    const auto tabCount = document_.tabs.size();
    if (filtering) {
        const auto tabRect = toD2dRect(layout.tabs);
        drawText(
            L"搜索结果 · " + std::to_wstring(searchResults_.size()),
            tabRect,
            tabFormat_.get(),
            textBrush_.get());
        const float center = (tabRect.left + tabRect.right) / 2.0F;
        renderTarget_->FillRectangle(
            D2D1::RectF(center - 48.0F, tabRect.bottom - 2.0F, center + 48.0F, tabRect.bottom),
            accentBrush_.get());
    }
    else {
        const float tabWidth = tabCount > 0
            ? layout.tabs.width / static_cast<float>(tabCount)
            : layout.tabs.width;
        for (std::size_t index = 0; index < tabCount; ++index) {
            const auto tabRect = D2D1::RectF(
                layout.tabs.x + static_cast<float>(index) * tabWidth,
                layout.tabs.y,
                layout.tabs.x + static_cast<float>(index + 1) * tabWidth,
                layout.tabs.y + layout.tabs.height);
            drawText(
                utf8ToWide(document_.tabs[index].name),
                tabRect,
                tabFormat_.get(),
                index == activeTabIndex_ ? textBrush_.get() : mutedTextBrush_.get());
            if (index == activeTabIndex_) {
                renderTarget_->FillRectangle(
                    D2D1::RectF(tabRect.left + 10.0F, tabRect.bottom - 2.0F, tabRect.right - 10.0F, tabRect.bottom),
                    accentBrush_.get());
            }
            if (itemDragActive_ && itemDropTarget_
                && itemDropTarget_->tabTarget && itemDropTarget_->tabIndex == index) {
                renderTarget_->DrawRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(
                            tabRect.left + 3.0F,
                            tabRect.top + 3.0F,
                            tabRect.right - 3.0F,
                            tabRect.bottom - 3.0F),
                        6.0F,
                        6.0F),
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
    const auto realItemCount = visibleItemCount();
    for (std::size_t index = 0; index < layout.items.size(); ++index) {
        const auto absoluteIndex = pageOffset_ + index;
        const auto displayed = displayedItem(absoluteIndex);
        const bool addTile = !filtering && index >= realItemCount;
        const auto name = addTile || !displayed || !displayed->item
            ? std::wstring{L"添加"}
            : utf8ToWide(displayed->item->name);
        const auto glyph = addTile ? std::wstring{L"+"} : itemGlyph(name);
        const auto color = addTile || !displayed || !displayed->item
            ? 0x64748BU
            : itemColor(displayed->item->type);
        const auto tile = toD2dRect(layout.items[index]);
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(tile, cornerRadius, cornerRadius),
            surfaceBrush_.get());
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(tile, cornerRadius, cornerRadius),
            borderBrush_.get(),
            1.0F);
        if (itemDragActive_ && itemDropTarget_ && !itemDropTarget_->tabTarget
            && itemDropTarget_->tabIndex == activeTabIndex_
            && itemDropTarget_->displayedTileIndex == index) {
            renderTarget_->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(
                        tile.left + 1.0F,
                        tile.top + 1.0F,
                        tile.right - 1.0F,
                        tile.bottom - 1.0F),
                    cornerRadius - 1.0F,
                    cornerRadius - 1.0F),
                accentBrush_.get(),
                3.0F);
        }
        const bool keyboardFocused = windowFocused_ || GetFocus() == searchWindow_.handle();
        if (!addTile && keyboardFocused && absoluteIndex == focusedItemIndex_) {
            const auto focusBounds = D2D1::RectF(
                tile.left + 2.0F,
                tile.top + 2.0F,
                tile.right - 2.0F,
                tile.bottom - 2.0F);
            renderTarget_->DrawRoundedRectangle(
                D2D1::RoundedRect(focusBounds, cornerRadius - 2.0F, cornerRadius - 2.0F),
                accentBrush_.get(),
                2.0F);
        }

        const auto iconRect = D2D1::RectF(
            tile.left + 14.0F,
            tile.top + 8.0F,
            tile.right - 14.0F,
            tile.top + 48.0F);
        auto* realIcon = !addTile && displayed && displayed->item
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
                D2D1::RoundedRect(iconRect, 14.0F, 14.0F),
                iconBrush ? iconBrush.get() : elevatedBrush_.get());
            drawText(
                glyph,
                iconRect,
                iconFormat_.get(),
                textBrush_.get());
        }
        if (filtering && displayed && displayed->tab) {
            drawText(
                name,
                D2D1::RectF(tile.left + 3.0F, tile.top + 50.0F, tile.right - 3.0F, tile.top + 69.0F),
                smallFormat_.get(),
                textBrush_.get());
            drawText(
                utf8ToWide(displayed->tab->name),
                D2D1::RectF(tile.left + 3.0F, tile.top + 66.0F, tile.right - 3.0F, tile.bottom - 2.0F),
                captionFormat_.get(),
                mutedTextBrush_.get());
        }
        else {
            drawText(
                name,
                D2D1::RectF(tile.left + 3.0F, tile.top + 54.0F, tile.right - 3.0F, tile.bottom - 4.0F),
                smallFormat_.get(),
                addTile ? mutedTextBrush_.get() : textBrush_.get());
        }
    }
    const auto totalItems = totalItemCount();
    const auto capacity = pageCapacity();
    if (capacity > 0 && totalItems > capacity) {
        const float trackTop = gridBounds.top + 4.0F;
        const float trackBottom = gridBounds.bottom - 4.0F;
        const float trackHeight = std::max(0.0F, trackBottom - trackTop);
        const float thumbHeight = std::max(
            24.0F,
            trackHeight * static_cast<float>(capacity) / static_cast<float>(totalItems));
        const auto maximumOffset = maximumPageOffset();
        const float progress = maximumOffset > 0
            ? static_cast<float>(pageOffset_) / static_cast<float>(maximumOffset)
            : 0.0F;
        const float thumbTop = trackTop + (trackHeight - thumbHeight) * progress;
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(gridBounds.right - 3.0F, trackTop, gridBounds.right - 1.0F, trackBottom),
                1.0F,
                1.0F),
            borderBrush_.get());
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(
                    gridBounds.right - 4.0F,
                    thumbTop,
                    gridBounds.right,
                    thumbTop + thumbHeight),
                2.0F,
                2.0F),
            accentBrush_.get());
    }
    if (filtering && searchResults_.empty()) {
        drawText(
            L"没有找到匹配项",
            D2D1::RectF(
                gridBounds.left,
                gridBounds.top + 92.0F,
                gridBounds.right,
                gridBounds.top + 128.0F),
            bodyFormat_.get(),
            mutedTextBrush_.get());
    }
    renderTarget_->PopAxisAlignedClip();

    const auto drawResult = renderTarget_->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
    }
    EndPaint(window_, &paint);
}

bool LauncherWindow::handleKeyDown(const WPARAM key)
{
    if (key == VK_INSERT && !isSearchFiltering()) {
        showAddEditor();
        return true;
    }
    if (key == VK_F2 && totalItemCount() > 0) {
        showEditEditor(focusedItemIndex_);
        return true;
    }
    if (key == VK_DELETE && totalItemCount() > 0) {
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
        activateFocusedItem();
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
            if (key == VK_PRIOR) {
                focusedItemIndex_ = focusedItemIndex_ > capacity
                    ? focusedItemIndex_ - capacity
                    : 0U;
            }
            else {
                focusedItemIndex_ = std::min(
                    focusedItemIndex_ + capacity,
                    itemCount - 1U);
            }
            ensureFocusedItemVisible();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return true;
    }
    if (key == VK_HOME || key == VK_END) {
        if (const auto visibleCount = visibleItemCount(); visibleCount > 0) {
            focusedItemIndex_ = key == VK_HOME
                ? pageOffset_
                : pageOffset_ + visibleCount - 1U;
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
    });
    if (const auto itemIndex = navigateGridItem(
            focusedItemIndex_,
            itemCount,
            layout.columns,
            *direction)) {
        focusedItemIndex_ = *itemIndex;
        ensureFocusedItemVisible();
        InvalidateRect(window_, nullptr, FALSE);
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
        activateFocusedItem();
        return true;
    }
    if (key == VK_TAB) {
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
    searchResults_.clear();
    if (!query.empty()) {
        if (const auto normalizedQuery = platform::windows::normalizeSearchText(query)) {
            searchResults_ = searchIndex_.search(*normalizedQuery, searchIndex_.size());
        }
    }
    focusedItemIndex_ = 0;
    pageOffset_ = 0;
    wheelDeltaRemainder_ = 0;
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::handleMouseWheel(const short delta)
{
    wheelDeltaRemainder_ += delta;
    const int steps = wheelDeltaRemainder_ / WHEEL_DELTA;
    wheelDeltaRemainder_ %= WHEEL_DELTA;
    const auto capacity = pageCapacity();
    if (steps == 0 || capacity == 0 || totalItemCount() <= capacity) {
        return;
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
    });
    const auto rowSize = std::min(layout.columns, capacity);
    const auto rowDistance = static_cast<std::ptrdiff_t>(rowSize)
        * static_cast<std::ptrdiff_t>(-steps);
    const auto requestedOffset = static_cast<std::ptrdiff_t>(pageOffset_) + rowDistance;
    pageOffset_ = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
        requestedOffset,
        0,
        static_cast<std::ptrdiff_t>(maximumPageOffset())));

    const auto visibleCount = visibleItemCount();
    if (visibleCount > 0) {
        if (focusedItemIndex_ < pageOffset_) {
            focusedItemIndex_ = pageOffset_;
        }
        else if (focusedItemIndex_ >= pageOffset_ + visibleCount) {
            focusedItemIndex_ = pageOffset_ + visibleCount - 1U;
        }
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void LauncherWindow::showAddEditor()
{
    auto edited = itemEditorHandler_
        ? itemEditorHandler_(window_, document_.tabs, activeTabIndex_, nullptr)
        : ItemEditorDialog::show(window_, document_.tabs, activeTabIndex_);
    if (!edited) {
        return;
    }
    auto id = platform::windows::createUuidV4();
    if (!id) {
        MessageBoxW(window_, L"无法生成条目标识。", L"HLaunch 条目", MB_OK | MB_ICONERROR);
        return;
    }
    edited->item.id = std::move(*id);
    auto updatedDocument = document_;
    const auto location = core::addItem(
        updatedDocument,
        edited->tabIndex,
        std::move(edited->item));
    if (!location || !core::validateItemsDocument(updatedDocument).empty()) {
        MessageBoxW(window_, L"条目内容未通过校验，请检查输入。", L"HLaunch 条目", MB_OK | MB_ICONWARNING);
        return;
    }
    document_ = std::move(updatedDocument);
    activeTabIndex_ = location->tabIndex;
    focusedItemIndex_ = location->itemIndex;
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
    core::ItemLocation source{};
    if (isSearchFiltering()) {
        if (absoluteIndex >= searchResults_.size()) return;
        source = {searchResults_[absoluteIndex].tabIndex, searchResults_[absoluteIndex].itemIndex};
    }
    else {
        source = {activeTabIndex_, absoluteIndex};
    }
    if (source.tabIndex >= document_.tabs.size()
        || source.itemIndex >= document_.tabs[source.tabIndex].items.size()) return;
    const auto* initial = &document_.tabs[source.tabIndex].items[source.itemIndex];
    auto edited = itemEditorHandler_
        ? itemEditorHandler_(window_, document_.tabs, source.tabIndex, initial)
        : ItemEditorDialog::show(window_, document_.tabs, source.tabIndex, initial);
    if (!edited) return;

    auto updatedDocument = document_;
    const auto location = core::updateItem(
        updatedDocument,
        source,
        edited->tabIndex,
        std::move(edited->item));
    if (!location || !core::validateItemsDocument(updatedDocument).empty()) {
        MessageBoxW(window_, L"条目内容未通过校验，请检查输入。", L"HLaunch 条目", MB_OK | MB_ICONWARNING);
        return;
    }
    document_ = std::move(updatedDocument);
    activeTabIndex_ = location->tabIndex;
    focusedItemIndex_ = location->itemIndex;
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
    else if (selected == static_cast<UINT>(ItemContextCommand::Edit)) {
        showEditEditor(absoluteIndex);
    }
    else if (selected == static_cast<UINT>(ItemContextCommand::Delete)) {
        deleteItem(absoluteIndex);
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
        : MessageBoxW(
              window_,
              prompt.c_str(),
              L"HLaunch 删除条目",
              MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
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
        MessageBoxW(
            window_,
            L"无法删除该条目，数据未发生变化。",
            L"HLaunch 删除条目",
            MB_OK | MB_ICONERROR);
        return;
    }

    document_ = std::move(updatedDocument);
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
    itemDragActive_ = false;
    SetCapture(window_);
}

void LauncherWindow::updateItemDrag(const POINT clientPoint)
{
    if (!itemDragSource_) {
        return;
    }
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
    });

    std::optional<InternalDropTarget> target{};
    if (const auto tabIndex = hitTestLauncherTab(
            layout,
            document_.tabs.size(),
            xDip,
            yDip)) {
        const auto targetSize = document_.tabs[*tabIndex].items.size();
        target = InternalDropTarget{
            .tabIndex = *tabIndex,
            .itemIndex = *tabIndex == itemDragSource_->tabIndex
                ? targetSize - 1U
                : targetSize,
            .tabTarget = true,
        };
    }
    else if (const auto displayedIndex = hitTestLauncherItem(layout, xDip, yDip)) {
        const auto itemCount = document_.tabs[activeTabIndex_].items.size();
        const auto requestedIndex = pageOffset_ + *displayedIndex;
        target = InternalDropTarget{
            .tabIndex = activeTabIndex_,
            .itemIndex = std::min(requestedIndex, itemCount - 1U),
            .tabTarget = false,
            .displayedTileIndex = *displayedIndex,
        };
    }

    if (target != itemDropTarget_) {
        itemDropTarget_ = target;
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void LauncherWindow::finishItemDrag(const POINT clientPoint)
{
    updateItemDrag(clientPoint);
    const auto source = itemDragSource_;
    const auto target = itemDropTarget_;
    cancelItemDrag();
    if (!source || !target
        || (source->tabIndex == target->tabIndex && source->itemIndex == target->itemIndex)) {
        return;
    }

    auto updatedDocument = document_;
    const auto location = core::moveItem(
        updatedDocument,
        *source,
        target->tabIndex,
        target->itemIndex);
    if (!location || !core::validateItemsDocument(updatedDocument).empty()) {
        return;
    }

    document_ = std::move(updatedDocument);
    activeTabIndex_ = location->tabIndex;
    focusedItemIndex_ = location->itemIndex;
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
    if (!isSearchFiltering()) {
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
            });
            const float xDip = static_cast<float>(clientPoint.x) * 96.0F
                / static_cast<float>(dpi_);
            const float yDip = static_cast<float>(clientPoint.y) * 96.0F
                / static_cast<float>(dpi_);
            if (const auto tab = hitTestLauncherTab(
                    layout,
                    document_.tabs.size(),
                    xDip,
                    yDip)) {
                targetTabIndex = *tab;
            }
        }
    }
    dropResolver_->submit({targetTabIndex, std::move(sources)});
}

void LauncherWindow::applyDropImport(platform::windows::DropImportResult result)
{
    if (result.failed) {
        MessageBoxW(
            window_,
            L"无法解析拖入内容。请检查文件或网址后重试。",
            L"HLaunch 拖放",
            MB_OK | MB_ICONERROR);
        return;
    }
    if (result.targetTabIndex >= document_.tabs.size()) {
        return;
    }
    if (result.items.empty()) {
        MessageBoxW(
            window_,
            L"拖入内容中没有可添加的文件、文件夹、快捷方式或网址。",
            L"HLaunch 拖放",
            MB_OK | MB_ICONINFORMATION);
        return;
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
        && MessageBoxW(
               window_,
               (L"发现 " + std::to_wstring(duplicateCount)
                   + L" 个启动属性完全相同的条目。\n\n是否仍然添加？"
                     L"选择“否”将跳过这些条目。")
                   .c_str(),
               L"HLaunch 重复条目",
               MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2)
            == IDYES;

    for (auto& item : result.items) {
        auto id = platform::windows::createUuidV4();
        if (!id) {
            MessageBoxW(window_, L"无法生成条目标识。", L"HLaunch 拖放", MB_OK | MB_ICONERROR);
            return;
        }
        item.id = std::move(*id);
    }

    auto updatedDocument = document_;
    const auto mutation = core::addImportedItems(
        updatedDocument,
        result.targetTabIndex,
        std::move(result.items),
        allowDuplicates);
    if (!mutation || !core::validateItemsDocument(updatedDocument).empty()) {
        MessageBoxW(
            window_,
            L"拖入条目超过数据限制或未通过校验，本次没有添加。",
            L"HLaunch 拖放",
            MB_OK | MB_ICONWARNING);
        return;
    }

    if (!mutation->added.empty()) {
        document_ = std::move(updatedDocument);
        activeTabIndex_ = result.targetTabIndex;
        focusedItemIndex_ = mutation->added.front().itemIndex;
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
        MessageBoxW(window_, summary.c_str(), L"HLaunch 拖放", MB_OK | MB_ICONINFORMATION);
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
    const auto cached = iconCache_.find(result.itemId);
    if (cached == iconCache_.end()
        || cached->second.sourceKey != result.sourceKey
        || cached->second.requestedPixelSize != result.requestedPixelSize) {
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
}

void LauncherWindow::activateFocusedItem()
{
    if (const auto displayed = displayedItem(focusedItemIndex_);
        displayed && displayed->item && launchHandler_) {
        launchHandler_(*displayed->item);
    }
}

void LauncherWindow::changeActiveTab(const std::size_t tabIndex)
{
    if (tabIndex >= document_.tabs.size()) {
        return;
    }
    activeTabIndex_ = tabIndex;
    focusedItemIndex_ = 0;
    pageOffset_ = 0;
    wheelDeltaRemainder_ = 0;
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
    if (!tab || index >= tab->items.size()) {
        return std::nullopt;
    }
    return core::ItemLocation{activeTabIndex_, index};
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
        return DisplayedItem{&tab.items[result.itemIndex], &tab};
    }

    const auto* tab = activeTab();
    if (!tab || index >= tab->items.size()) {
        return std::nullopt;
    }
    return DisplayedItem{&tab->items[index], tab};
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
    return tab ? tab->items.size() : 0U;
}

std::size_t LauncherWindow::pageCapacity() const
{
    if (!window_) {
        return maximumVisibleItems;
    }
    RECT client{};
    if (!GetClientRect(window_, &client)) {
        return maximumVisibleItems;
    }
    const float widthDip = static_cast<float>(client.right - client.left) * 96.0F
        / static_cast<float>(dpi_);
    const float heightDip = static_cast<float>(client.bottom - client.top) * 96.0F
        / static_cast<float>(dpi_);
    const auto availableCapacity = calculateLauncherGridCapacity(widthDip, heightDip);
    const auto limitedCapacity = std::min(
        maximumVisibleItems,
        availableCapacity);
    const auto layout = calculateLauncherLayout({
        .clientWidthDip = widthDip,
        .clientHeightDip = heightDip,
        .itemCount = 0,
    });
    return limitedCapacity >= layout.columns
        ? (limitedCapacity / layout.columns) * layout.columns
        : limitedCapacity;
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
    });
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
    focusedItemIndex_ = std::min(focusedItemIndex_, itemCount - 1U);
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
    });
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
    if (isSearchFiltering()) {
        return visibleItemCount();
    }
    if (!activeTab()) {
        return 0;
    }
    const auto capacity = pageCapacity();
    const auto itemCount = visibleItemCount();
    if (capacity == 0 || pageOffset_ + itemCount < totalItemCount()
        || itemCount >= capacity) {
        return itemCount;
    }
    return itemCount + 1U;
}

ID2D1Bitmap* LauncherWindow::itemIconBitmap(const core::LaunchItem& item)
{
    const auto sourceKey = iconSourceKey(item);
    const auto requestedPixelSize = static_cast<std::uint32_t>(std::clamp(
        MulDiv(48, static_cast<int>(dpi_), 96),
        16,
        256));
    auto cached = iconCache_.find(item.id);
    if (cached == iconCache_.end()
        || cached->second.sourceKey != sourceKey
        || cached->second.requestedPixelSize != requestedPixelSize) {
        if (cached == iconCache_.end() && iconCache_.size() >= maximumIconCacheEntries) {
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
            .pending = iconLoader_ != nullptr,
            .failed = iconLoader_ == nullptr,
        };
        cached = iconCache_.insert_or_assign(item.id, std::move(replacement)).first;
        if (iconLoader_) {
            iconLoader_->submit({
                .itemId = item.id,
                .sourceKey = sourceKey,
                .target = item.target,
                .icon = item.icon,
                .pixelSize = requestedPixelSize,
            });
        }
    }

    auto& entry = cached->second;
    entry.lastUsed = ++iconCacheUseSequence_;
    if (entry.pending || entry.failed || entry.pixels.empty() || !renderTarget_) {
        return nullptr;
    }
    if (!entry.bitmap) {
        const auto properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi_),
            static_cast<float>(dpi_));
        if (FAILED(renderTarget_->CreateBitmap(
                D2D1::SizeU(entry.width, entry.height),
                entry.pixels.data(),
                entry.width * 4U,
                properties,
                entry.bitmap.put()))) {
            return nullptr;
        }
    }
    return entry.bitmap.get();
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
