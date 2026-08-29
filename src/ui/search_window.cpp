#include "ui/search_window.h"

#include "ui/visual_style.h"
#include "ui/system_appearance.h"

#include <d2d1helper.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr wchar_t searchWindowClass[] = L"HLaunch.SearchWindow.v1";
constexpr float fieldCornerRadius = 4.0F;
constexpr std::size_t maximumQueryLength = 256;
constexpr int searchEditId = 4101;

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
    QueryChangedHandler queryChangedHandler,
    KeyHandler keyHandler)
{
    queryChangedHandler_ = std::move(queryChangedHandler);
    keyHandler_ = std::move(keyHandler);
    windowEffects_ = effects;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    windowClass.lpfnWndProc = &SearchWindow::windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
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

    const auto effectiveEffects = effectiveWindowEffects(effects);
    translucentSurface_ = effectiveEffects.backdrop
        != platform::windows::WindowBackdrop::Solid;
    static_cast<void>(platform::windows::applyWindowEffects(window_, effectiveEffects));
    applyNativeWindowStyle(window_, true, true);
    applyEditSurface();
    applyEditFont();
    return true;
}

void SearchWindow::setWindowEffects(
    const platform::windows::WindowEffects& effects)
{
    if (windowEffects_ == effects) {
        return;
    }
    windowEffects_ = effects;
    refreshSystemAppearance();
}

void SearchWindow::refreshSystemAppearance()
{
    if (!window_) {
        return;
    }
    const auto effects = effectiveWindowEffects(windowEffects_);
    translucentSurface_ = effects.backdrop
        != platform::windows::WindowBackdrop::Solid;
    static_cast<void>(platform::windows::applyWindowEffects(window_, effects));
    applyNativeWindowStyle(window_, true, true);
    const bool highContrast = isHighContrastEnabled();
    if (highContrast) {
        applyNativeControlStyle(edit_, false);
    }
    else if (edit_) {
        // Let WM_CTLCOLOREDIT paint the same surface as the surrounding field.
        // A themed EDIT draws its own opaque rectangle over the custom search UI.
        static_cast<void>(SetWindowTheme(edit_, L"", L""));
    }
    applyEditSurface();
    static_cast<void>(createTextFormat());
    applyEditFont();
    discardDeviceResources();
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void SearchWindow::show()
{
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    SetFocus(edit_ ? edit_ : window_);
    if (edit_) {
        const auto end = static_cast<LPARAM>(query_.size());
        SendMessageW(edit_, EM_SETSEL, end, end);
    }
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
    positionEditControl();
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
    if (edit_) {
        suppressEditChange_ = true;
        SetWindowTextW(edit_, query_.c_str());
        const auto end = static_cast<LPARAM>(query_.size());
        SendMessageW(edit_, EM_SETSEL, end, end);
        suppressEditChange_ = false;
    }
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
    const LPARAM lParam) noexcept
{
    try {
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
        return self->handleMessage(window, message, wParam, lParam);
    }
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch search window callback failed.\n");
        if (message == WM_NCCREATE) return FALSE;
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SearchWindow::handleMessage(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        if (!createDeviceIndependentResources()) return -1;
        createEditControl();
        refreshSystemAppearance();
        return edit_ ? 0 : -1;
    case WM_PAINT:
        render();
        return 0;
    case WM_ERASEBKGND: // NOLINT(bugprone-branch-clone): TRUE and HTCLIENT both equal 1 but represent different Win32 contracts.
        return 1;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS | DLGC_WANTALLKEYS;
    case WM_SETFOCUS:
        SetFocus(edit_);
        return 0;
    case WM_KILLFOCUS:
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        SetFocus(edit_);
        return 0;
    case WM_MOUSEWHEEL:
        if (const auto owner = GetWindow(window_, GW_OWNER)) {
            return SendMessageW(owner, message, wParam, lParam);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == searchEditId && HIWORD(wParam) == EN_CHANGE && !suppressEditChange_) {
            const int length = GetWindowTextLengthW(edit_);
            std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
            const int copied = GetWindowTextW(edit_, value.data(), static_cast<int>(value.size()));
            value.resize(static_cast<std::size_t>(std::max(0, copied)));
            query_ = std::move(value);
            notifyQueryChanged();
            RedrawWindow(
                window_, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_CTLCOLOREDIT: {
        const auto dc = reinterpret_cast<HDC>(wParam);
        const bool highContrast = isHighContrastEnabled();
        const auto palette = launcherPalette(highContrast);
        SetTextColor(dc, toColorRef(palette.text));
        SetBkColor(dc, toColorRef(palette.inputBackground));
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(editBackgroundBrush_.get());
    }
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
            auto updated = query_;
            updated.push_back(static_cast<wchar_t>(wParam));
            setQuery(std::move(updated));
        }
        return 0;
    case WM_UNICHAR:
        if (wParam == UNICODE_NOCHAR) {
            return TRUE;
        }
        if (wParam >= 0x20 && wParam <= 0x10FFFF) {
            auto updated = query_;
            bool changed = false;
            if (wParam <= 0xFFFF && updated.size() < maximumQueryLength) {
                updated.push_back(static_cast<wchar_t>(wParam));
                changed = true;
            }
            else if (updated.size() + 1U < maximumQueryLength) {
                const auto codePoint = static_cast<unsigned long>(wParam) - 0x10000UL;
                updated.push_back(static_cast<wchar_t>(0xD800UL + (codePoint >> 10U)));
                updated.push_back(static_cast<wchar_t>(0xDC00UL + (codePoint & 0x3FFUL)));
                changed = true;
            }
            if (changed) {
                setQuery(std::move(updated));
            }
        }
        return 0;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_SIZE:
        if (renderTarget_ && wParam != SIZE_MINIMIZED) {
            renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        }
        positionEditControl();
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
        refreshSystemAppearance();
        return 0;
    }
    case WM_CLOSE:
        hide();
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_NCDESTROY:
        if (edit_) { RemoveWindowSubclass(edit_, editSubclassProcedure, 1); edit_ = nullptr; }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
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

    return createTextFormat();
}

bool SearchWindow::createTextFormat()
{
    if (!writeFactory_) {
        return false;
    }
    bodyFormat_ = nullptr;
    const auto fontFamily = systemUiFontFamily(dpi_);
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
    bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return true;
}

void SearchWindow::applyEditFont()
{
    static_cast<void>(editFont_.refresh(dpi_));
    const auto font = editFont_.get();
    if (edit_ && font) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
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

    const auto palette = launcherPalette(isHighContrastEnabled());
    const struct BrushDefinition {
        std::uint32_t color;
        float opacity;
        winrt::com_ptr<ID2D1SolidColorBrush>* destination;
    } brushes[]{
        {palette.background, palette.backgroundAlpha * (translucentSurface_ ? 0.70F : 1.0F), &backgroundBrush_},
        {palette.inputBackground, 1.0F, &surfaceBrush_},
        {palette.textMuted, palette.textMutedAlpha, &textBrush_},
        {palette.text, palette.textAlpha, &queryTextBrush_},
        {palette.inputBorder, 1.0F, &borderBrush_},
        {palette.focus, 1.0F, &focusBrush_},
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
    focusBrush_ = nullptr;
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

void SearchWindow::createEditControl()
{
    edit_ = CreateWindowExW(0, L"EDIT", query_.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(searchEditId)),
        GetModuleHandleW(nullptr), nullptr);
    if (!edit_) return;
    SendMessageW(edit_, EM_SETLIMITTEXT, maximumQueryLength, 0);
    SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜索全部分类"));
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
    SetWindowSubclass(edit_, editSubclassProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    positionEditControl();
}

void SearchWindow::applyEditSurface()
{
    const auto palette = launcherPalette(isHighContrastEnabled());
    editBackgroundBrush_.reset(CreateSolidBrush(toColorRef(palette.inputBackground)));
    if (!edit_) {
        return;
    }

    // The search host uses a premultiplied-alpha D2D target for system
    // backdrops. A normal GDI child EDIT updates RGB but can leave its
    // redirected alpha undefined, which turns #303030 into a light gray after
    // DWM composition. Apply the layered style after the parent CreateWindowEx
    // has returned; Windows ignores this transition during the parent's
    // WM_CREATE. A fully opaque child keeps native text and IME behavior.
    const auto extendedStyle = GetWindowLongPtrW(edit_, GWL_EXSTYLE);
    if ((extendedStyle & WS_EX_LAYERED) == 0) {
        SetWindowLongPtrW(edit_, GWL_EXSTYLE, extendedStyle | WS_EX_LAYERED);
        SetWindowPos(
            edit_, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE
                | SWP_NOACTIVATE | SWP_NOZORDER);
    }
    static_cast<void>(SetLayeredWindowAttributes(edit_, 0, 255, LWA_ALPHA));
}

void SearchWindow::positionEditControl() noexcept
{
    if (!edit_) return;
    SetWindowPos(edit_, nullptr, dipToPixels(layout_.edit.x, dpi_),
        dipToPixels(layout_.edit.y, dpi_),
        dipToPixels(layout_.edit.width, dpi_),
        dipToPixels(layout_.edit.height, dpi_), SWP_NOACTIVATE | SWP_NOZORDER);
}

LRESULT CALLBACK SearchWindow::editSubclassProcedure(const HWND edit, const UINT message,
    const WPARAM wParam, const LPARAM lParam, UINT_PTR, const DWORD_PTR data) noexcept
{
    try {
        const auto self = reinterpret_cast<SearchWindow*>(data);
        if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            return TRUE;
        }
        if (message == WM_MOUSEWHEEL && self) return SendMessageW(GetWindow(self->window_, GW_OWNER), message, wParam, lParam);
        if (message == WM_KEYDOWN && self && self->keyHandler_ && self->keyHandler_(wParam)) return 0;
        if ((message == WM_SETFOCUS || message == WM_KILLFOCUS) && self) {
            InvalidateRect(self->window_, nullptr, FALSE);
        }
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch search edit callback failed.\n");
    }
    return DefSubclassProc(edit, message, wParam, lParam);
}

void SearchWindow::eraseLastCharacter()
{
    if (query_.empty()) {
        return;
    }
    auto updated = query_;
    updated.pop_back();
    if (!updated.empty() && IS_HIGH_SURROGATE(updated.back())) {
        updated.pop_back();
    }
    setQuery(std::move(updated));
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

    auto updated = query_;
    bool changed = false;
    for (std::size_t index = 0; text[index] != L'\0' && updated.size() < maximumQueryLength; ++index) {
        wchar_t character = text[index];
        if (character == L'\r' || character == L'\n' || character == L'\t') {
            character = L' ';
        }
        if (character >= 0x20) {
            updated.push_back(character);
            changed = true;
        }
    }
    GlobalUnlock(static_cast<HGLOBAL>(textHandle));
    CloseClipboard();
    if (changed) {
        setQuery(std::move(updated));
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
    const auto palette = launcherPalette(isHighContrastEnabled());
    if (translucentSurface_) {
        renderTarget_->Clear(D2D1::ColorF(0x000000, 0.0F));
    }
    else {
        renderTarget_->Clear(D2D1::ColorF(palette.background, 1.0F));
    }

    const auto field = toD2dRect(layout_.field);
    renderTarget_->FillRoundedRectangle(
        D2D1::RoundedRect(field, fieldCornerRadius, fieldCornerRadius),
        surfaceBrush_.get());
    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(field, fieldCornerRadius, fieldCornerRadius),
        GetFocus() == edit_ ? focusBrush_.get() : borderBrush_.get(),
        GetFocus() == edit_ ? 1.5F : 1.0F);
    const float iconCenterY = (field.top + field.bottom) / 2.0F;
    const D2D1_ELLIPSE searchCircle{
        D2D1::Point2F(field.left + 20.0F, iconCenterY - 1.0F),
        5.0F,
        5.0F,
    };
    renderTarget_->DrawEllipse(searchCircle, textBrush_.get(), 1.5F);
    renderTarget_->DrawLine(
        D2D1::Point2F(field.left + 23.5F, iconCenterY + 2.5F),
        D2D1::Point2F(field.left + 28.0F, iconCenterY + 7.0F),
        textBrush_.get(),
        1.5F);
    const auto drawResult = renderTarget_->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
    }
    EndPaint(window_, &paint);
}

} // namespace hlaunch::ui
