#include "ui/search_window.h"

#include "ui/theme.h"

#include <d2d1helper.h>
#include <windowsx.h>

#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr wchar_t searchWindowClass[] = L"HLaunch.SearchWindow.v1";
constexpr float cornerRadius = 12.0F;
constexpr std::size_t maximumQueryLength = 256;

int dipToPixels(const float dip, const UINT dpi) noexcept
{
    return static_cast<int>(std::lround(dip * static_cast<float>(dpi) / 96.0F));
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

SearchWindow::~SearchWindow()
{
    if (window_) {
        DestroyWindow(window_);
    }
}

bool SearchWindow::create(
    const HINSTANCE instance,
    const HWND owner,
    const platform::windows::WindowEffects& effects,
    const core::ThemeMode themeMode,
    QueryChangedHandler queryChangedHandler,
    KeyHandler keyHandler)
{
    queryChangedHandler_ = std::move(queryChangedHandler);
    keyHandler_ = std::move(keyHandler);
    themeMode_ = themeMode;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    windowClass.lpfnWndProc = &SearchWindow::windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    windowClass.lpszClassName = searchWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    translucentSurface_ = effects.backdrop != platform::windows::WindowBackdrop::Solid;
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        searchWindowClass,
        L"HLaunch Search",
        WS_POPUP,
        0,
        0,
        1,
        1,
        owner,
        nullptr,
        instance,
        this);
    if (!window_) {
        return false;
    }

    static_cast<void>(platform::windows::applyWindowEffects(window_, effects));
    applyNativeWindowTheme(window_, themeMode_);
    return true;
}

void SearchWindow::setThemeMode(const core::ThemeMode themeMode)
{
    if (themeMode_ == themeMode) {
        return;
    }
    themeMode_ = themeMode;
    applyNativeWindowTheme(window_, themeMode_);
    discardDeviceResources();
    InvalidateRect(window_, nullptr, FALSE);
}

void SearchWindow::show()
{
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    SetFocus(window_);
    InvalidateRect(window_, nullptr, FALSE);
}

void SearchWindow::positionAttached(
    const HWND owner,
    const UINT dpi,
    const SearchPopupLayout& layout)
{
    if (!window_ || !owner) {
        return;
    }

    layout_ = layout;
    dpi_ = dpi;
    RECT ownerBounds{};
    if (!GetWindowRect(owner, &ownerBounds)) {
        return;
    }

    SetWindowPos(
        window_,
        nullptr,
        ownerBounds.left + dipToPixels(layout.xOffsetDip, dpi),
        ownerBounds.bottom + dipToPixels(layout.gapDip, dpi),
        dipToPixels(layout.windowWidthDip, dpi),
        dipToPixels(layout.windowHeightDip, dpi),
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    if (renderTarget_) {
        renderTarget_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    }
}

void SearchWindow::hide()
{
    if (window_) {
        ShowWindow(window_, SW_HIDE);
    }
}

void SearchWindow::setQuery(std::wstring query)
{
    if (query.size() > maximumQueryLength) {
        query.resize(maximumQueryLength);
    }
    if (query_ == query) {
        return;
    }
    query_ = std::move(query);
    notifyQueryChanged();
    InvalidateRect(window_, nullptr, FALSE);
}

HWND SearchWindow::handle() const noexcept
{
    return window_;
}

bool SearchWindow::isVisible() const noexcept
{
    return window_ && IsWindowVisible(window_);
}

std::wstring_view SearchWindow::query() const noexcept
{
    return query_;
}

LRESULT CALLBACK SearchWindow::windowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    SearchWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam); // NOLINT(performance-no-int-to-ptr): Win32 LPARAM carries this pointer.
        self = static_cast<SearchWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else {
        self = reinterpret_cast<SearchWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr): Win32 stores this pointer as LONG_PTR.
    }

    if (self) {
        return self->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SearchWindow::handleMessage(
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        return createDeviceIndependentResources() ? 0 : -1;
    case WM_PAINT:
        render();
        return 0;
    case WM_ERASEBKGND: // NOLINT(bugprone-branch-clone): TRUE and HTCLIENT both equal 1 but represent different Win32 contracts.
        return 1;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS | DLGC_WANTALLKEYS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        SetFocus(window_);
        return 0;
    case WM_MOUSEWHEEL:
        if (const auto owner = GetWindow(window_, GW_OWNER)) {
            return SendMessageW(owner, message, wParam, lParam);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_BACK) {
            eraseLastCharacter();
            return 0;
        }
        if (wParam == L'V' && GetKeyState(VK_CONTROL) < 0) {
            pasteClipboardText();
            return 0;
        }
        if (keyHandler_ && keyHandler_(wParam)) {
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    case WM_CHAR:
        if (wParam >= 0x20 && wParam != 0x7F && query_.size() < maximumQueryLength) {
            query_.push_back(static_cast<wchar_t>(wParam));
            notifyQueryChanged();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_UNICHAR:
        if (wParam == UNICODE_NOCHAR) {
            return TRUE;
        }
        if (wParam >= 0x20 && wParam <= 0x10FFFF) {
            bool changed = false;
            if (wParam <= 0xFFFF && query_.size() < maximumQueryLength) {
                query_.push_back(static_cast<wchar_t>(wParam));
                changed = true;
            }
            else if (query_.size() + 1U < maximumQueryLength) {
                const auto codePoint = static_cast<unsigned long>(wParam) - 0x10000UL;
                query_.push_back(static_cast<wchar_t>(0xD800UL + (codePoint >> 10U)));
                query_.push_back(static_cast<wchar_t>(0xDC00UL + (codePoint & 0x3FFUL)));
                changed = true;
            }
            if (changed) {
                notifyQueryChanged();
                InvalidateRect(window_, nullptr, FALSE);
            }
        }
        return 0;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_SIZE:
        if (renderTarget_ && wParam != SIZE_MINIMIZED) {
            renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        }
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
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
        if (renderTarget_) {
            renderTarget_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        }
        return 0;
    }
    case WM_CLOSE:
        hide();
        return 0;
    case WM_DESTROY:
        window_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

bool SearchWindow::createDeviceIndependentResources()
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

    if (FAILED(writeFactory_->CreateTextFormat(
            L"Segoe UI Variable Text",
            nullptr,
            DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.0F,
            L"zh-CN",
            bodyFormat_.put()))) {
        return false;
    }
    bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return true;
}

bool SearchWindow::createDeviceResources()
{
    if (renderTarget_) {
        return true;
    }

    RECT client{};
    GetClientRect(window_, &client);
    const auto size = D2D1::SizeU(
        static_cast<UINT32>(client.right - client.left),
        static_cast<UINT32>(client.bottom - client.top));
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM,
            translucentSurface_ ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE),
        static_cast<float>(dpi_),
        static_cast<float>(dpi_));
    if (FAILED(d2dFactory_->CreateHwndRenderTarget(
            properties,
            D2D1::HwndRenderTargetProperties(window_, size),
            renderTarget_.put()))) {
        return false;
    }

    const auto& palette = paletteFor(themeMode_);
    const bool light = themeMode_ == core::ThemeMode::Light;
    const struct BrushDefinition {
        std::uint32_t color;
        float opacity;
        winrt::com_ptr<ID2D1SolidColorBrush>* destination;
    } brushes[]{
        {palette.background, translucentSurface_ ? (light ? 0.90F : 0.70F) : 1.0F, &backgroundBrush_},
        {palette.surface, translucentSurface_ ? (light ? 0.94F : 0.86F) : 1.0F, &surfaceBrush_},
        {palette.textMuted, 1.0F, &textBrush_},
        {palette.text, 1.0F, &queryTextBrush_},
        {palette.border, translucentSurface_ ? (light ? 0.92F : 0.68F) : 1.0F, &borderBrush_},
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

void SearchWindow::discardDeviceResources() noexcept
{
    borderBrush_ = nullptr;
    queryTextBrush_ = nullptr;
    textBrush_ = nullptr;
    surfaceBrush_ = nullptr;
    backgroundBrush_ = nullptr;
    renderTarget_ = nullptr;
}

void SearchWindow::notifyQueryChanged()
{
    if (queryChangedHandler_) {
        queryChangedHandler_(query_);
    }
}

void SearchWindow::eraseLastCharacter()
{
    if (query_.empty()) {
        return;
    }
    query_.pop_back();
    if (!query_.empty() && IS_HIGH_SURROGATE(query_.back())) {
        query_.pop_back();
    }
    notifyQueryChanged();
    InvalidateRect(window_, nullptr, FALSE);
}

void SearchWindow::pasteClipboardText()
{
    if (!OpenClipboard(window_)) {
        return;
    }
    const HANDLE textHandle = GetClipboardData(CF_UNICODETEXT);
    if (!textHandle) {
        CloseClipboard();
        return;
    }
    const auto* text = static_cast<const wchar_t*>(
        GlobalLock(static_cast<HGLOBAL>(textHandle)));
    if (!text) {
        CloseClipboard();
        return;
    }

    bool changed = false;
    for (std::size_t index = 0; text[index] != L'\0' && query_.size() < maximumQueryLength; ++index) {
        wchar_t character = text[index];
        if (character == L'\r' || character == L'\n' || character == L'\t') {
            character = L' ';
        }
        if (character >= 0x20) {
            query_.push_back(character);
            changed = true;
        }
    }
    GlobalUnlock(static_cast<HGLOBAL>(textHandle));
    CloseClipboard();
    if (changed) {
        notifyQueryChanged();
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void SearchWindow::render()
{
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (!createDeviceResources()) {
        EndPaint(window_, &paint);
        return;
    }

    renderTarget_->BeginDraw();
    const auto& palette = paletteFor(themeMode_);
    if (translucentSurface_) {
        renderTarget_->Clear(D2D1::ColorF(0x000000, 0.0F));
    }
    else {
        renderTarget_->Clear(D2D1::ColorF(palette.background, 1.0F));
    }

    const auto field = toD2dRect(layout_.field);
    renderTarget_->FillRoundedRectangle(
        D2D1::RoundedRect(field, cornerRadius, cornerRadius),
        surfaceBrush_.get());
    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(field, cornerRadius, cornerRadius),
        borderBrush_.get(),
        1.0F);
    const D2D1_ELLIPSE searchCircle{
        D2D1::Point2F(field.left + 23.0F, field.top + 22.0F),
        6.0F,
        6.0F,
    };
    renderTarget_->DrawEllipse(searchCircle, textBrush_.get(), 1.5F);
    renderTarget_->DrawLine(
        D2D1::Point2F(field.left + 27.0F, field.top + 26.0F),
        D2D1::Point2F(field.left + 32.0F, field.top + 31.0F),
        textBrush_.get(),
        1.5F);
    constexpr std::wstring_view searchPrompt = L"搜索全部分类";
    std::wstring displayedText{};
    ID2D1Brush* displayedBrush = textBrush_.get();
    if (query_.empty()) {
        displayedText = searchPrompt;
    }
    else {
        displayedText = query_;
        if (GetFocus() == window_) {
            displayedText += L"|";
        }
        displayedBrush = queryTextBrush_.get();
    }
    renderTarget_->DrawTextW(
        displayedText.data(),
        static_cast<UINT32>(displayedText.size()),
        bodyFormat_.get(),
        D2D1::RectF(field.left + 46.0F, field.top, field.right - 16.0F, field.bottom),
        displayedBrush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP,
        DWRITE_MEASURING_MODE_NATURAL);

    const auto drawResult = renderTarget_->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
    }
    EndPaint(window_, &paint);
}

} // namespace hlaunch::ui
