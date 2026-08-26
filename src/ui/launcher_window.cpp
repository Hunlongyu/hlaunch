#include "ui/launcher_window.h"

#include "ui/launcher_layout.h"

#include <d2d1helper.h>
#include <windowsx.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace hlaunch::ui {
namespace {

constexpr wchar_t launcherWindowClass[] = L"HLaunch.LauncherWindow.v1";
constexpr float cornerRadius = 12.0F;
constexpr int launcherWidthDip = 420;
constexpr int launcherHeightDip = 640;

struct PreviewItem {
    std::wstring_view name{};
    std::wstring_view glyph{};
    std::uint32_t color{};
};

constexpr std::array previewItems{
    PreviewItem{L"浏览器", L"B", 0x3B82F6},
    PreviewItem{L"终端", L">_", 0x14B8A6},
    PreviewItem{L"文件", L"F", 0x8B5CF6},
    PreviewItem{L"笔记", L"N", 0xF59E0B},
    PreviewItem{L"设计", L"D", 0xEC4899},
    PreviewItem{L"项目", L"P", 0x22C55E},
    PreviewItem{L"链接", L"L", 0x06B6D4},
    PreviewItem{L"邮件", L"M", 0xF97316},
    PreviewItem{L"日历", L"C", 0x0EA5E9},
    PreviewItem{L"计算器", L"=", 0x6366F1},
    PreviewItem{L"音乐", L"M", 0xD946EF},
    PreviewItem{L"图片", L"I", 0x10B981},
    PreviewItem{L"下载", L"↓", 0x0284C7},
    PreviewItem{L"设置", L"S", 0x64748B},
    PreviewItem{L"回收站", L"R", 0x475569},
    PreviewItem{L"代码", L"C", 0x2563EB},
    PreviewItem{L"远程", L"R", 0x0891B2},
    PreviewItem{L"工具", L"T", 0x7C3AED},
    PreviewItem{L"文档", L"W", 0x16A34A},
    PreviewItem{L"视频", L"V", 0xDB2777},
    PreviewItem{L"命令", L">", 0x0F766E},
    PreviewItem{L"监视", L"M", 0x0369A1},
    PreviewItem{L"压缩", L"Z", 0xB45309},
    PreviewItem{L"便携", L"P", 0x4F46E5},
    PreviewItem{L"添加", L"+", 0x64748B},
};

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
    if (window_) {
        DestroyWindow(window_);
    }
}

bool LauncherWindow::create(
    const HINSTANCE instance,
    const platform::windows::WindowEffects& effects,
    const bool showSearch)
{
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
    if (!searchWindow_.create(instance, window_, effects)) {
        return false;
    }
    searchVisible_ = showSearch;
    return true;
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

void LauncherWindow::hide()
{
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
        const auto layout = calculateLauncherLayout({widthDip, heightDip, 0});
        return isLauncherDragRegion(layout, xDip, yDip) ? HTCAPTION : HTCLIENT;
    }
    case WM_LBUTTONUP: {
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
        const auto layout = calculateLauncherLayout({widthDip, heightDip, 0});
        const auto& closeButton = layout.closeButton;
        if (xDip >= closeButton.x && xDip < closeButton.x + closeButton.width
            && yDip >= closeButton.y && yDip < closeButton.y + closeButton.height) {
            DestroyWindow(window_);
        }
        return 0;
    }
    case WM_SIZE:
        if (renderTarget_ && wParam != SIZE_MINIMIZED) {
            renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        }
        if (wParam != SIZE_MINIMIZED) {
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
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
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
        .itemCount = previewItems.size(),
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
        .itemCount = previewItems.size(),
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

    constexpr std::array tabNames{L"全部", L"常用", L"工作", L"工具"};
    const float tabWidth = layout.tabs.width / static_cast<float>(tabNames.size());
    for (std::size_t index = 0; index < tabNames.size(); ++index) {
        const auto tabRect = D2D1::RectF(
            layout.tabs.x + static_cast<float>(index) * tabWidth,
            layout.tabs.y,
            layout.tabs.x + static_cast<float>(index + 1) * tabWidth,
            layout.tabs.y + layout.tabs.height);
        drawText(
            tabNames[index],
            tabRect,
            tabFormat_.get(),
            index == 0 ? textBrush_.get() : mutedTextBrush_.get());
        if (index == 0) {
            renderTarget_->FillRectangle(
                D2D1::RectF(tabRect.left + 10.0F, tabRect.bottom - 2.0F, tabRect.right - 10.0F, tabRect.bottom),
                accentBrush_.get());
        }
    }

    const auto gridBounds = toD2dRect(layout.grid);
    renderTarget_->PushAxisAlignedClip(gridBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (std::size_t index = 0; index < layout.items.size(); ++index) {
        const auto tile = toD2dRect(layout.items[index]);
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(tile, cornerRadius, cornerRadius),
            surfaceBrush_.get());
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(tile, cornerRadius, cornerRadius),
            borderBrush_.get(),
            1.0F);

        winrt::com_ptr<ID2D1SolidColorBrush> iconBrush{};
        renderTarget_->CreateSolidColorBrush(
            D2D1::ColorF(previewItems[index].color, 0.92F),
            iconBrush.put());
        const auto iconRect = D2D1::RectF(
            tile.left + 14.0F,
            tile.top + 8.0F,
            tile.right - 14.0F,
            tile.top + 48.0F);
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(iconRect, 14.0F, 14.0F),
            iconBrush ? iconBrush.get() : elevatedBrush_.get());
        drawText(
            previewItems[index].glyph,
            iconRect,
            iconFormat_.get(),
            textBrush_.get());
        drawText(
            previewItems[index].name,
            D2D1::RectF(tile.left + 3.0F, tile.top + 54.0F, tile.right - 3.0F, tile.bottom - 4.0F),
            smallFormat_.get(),
            index + 1 == previewItems.size() ? mutedTextBrush_.get() : textBrush_.get());
    }
    renderTarget_->PopAxisAlignedClip();

    const auto drawResult = renderTarget_->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
    }
    EndPaint(window_, &paint);
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
