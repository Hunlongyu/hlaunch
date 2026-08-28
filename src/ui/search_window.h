#pragma once

#include "platform/windows/window_effects.h"
#include "ui/launcher_layout.h"
#include "ui/system_appearance.h"
#include "ui/visual_style.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <CommCtrl.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace hlaunch::ui {

class SearchWindow final {
public:
    using QueryChangedHandler = std::function<void(std::wstring_view)>;
    using KeyHandler = std::function<bool(WPARAM)>;

    SearchWindow() = default;
    ~SearchWindow();

    SearchWindow(const SearchWindow&) = delete;
    SearchWindow& operator=(const SearchWindow&) = delete;

    [[nodiscard]] bool create(
        HINSTANCE instance,
        HWND owner,
        const platform::windows::WindowEffects& effects,
        QueryChangedHandler queryChangedHandler,
        KeyHandler keyHandler);
    void setWindowEffects(const platform::windows::WindowEffects& effects);
    void refreshSystemAppearance();
    void show();
    void positionAttached(HWND owner, UINT dpi, const SearchPopupLayout& layout);
    void hide();
    void setQuery(std::wstring query);

    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] bool isVisible() const noexcept;
    [[nodiscard]] std::wstring_view query() const noexcept;

private:
    static LRESULT CALLBACK windowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) noexcept;
    LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK editSubclassProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;

    [[nodiscard]] bool createDeviceIndependentResources();
    [[nodiscard]] bool createTextFormat();
    [[nodiscard]] bool createDeviceResources();
    void discardDeviceResources() noexcept;
    void createEditControl();
    void applyEditSurface();
    void applyEditFont();
    void positionEditControl() noexcept;
    void notifyQueryChanged();
    void eraseLastCharacter();
    void pasteClipboardText();
    void render();

    HWND window_{};
    HWND edit_{};
    UINT dpi_{96};
    bool translucentSurface_{true};
    bool suppressEditChange_{};
    platform::windows::WindowEffects windowEffects_{};
    SearchPopupLayout layout_{};
    std::wstring query_{};
    QueryChangedHandler queryChangedHandler_{};
    KeyHandler keyHandler_{};
    SystemUiFont editFont_{};
    wil::unique_hbrush editBackgroundBrush_{};
    winrt::com_ptr<ID2D1Factory> d2dFactory_{};
    winrt::com_ptr<IDWriteFactory> writeFactory_{};
    winrt::com_ptr<ID2D1HwndRenderTarget> renderTarget_{};
    winrt::com_ptr<IDWriteTextFormat> bodyFormat_{};
    winrt::com_ptr<ID2D1SolidColorBrush> backgroundBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> surfaceBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> textBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> queryTextBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> borderBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> focusBrush_{};
};

} // namespace hlaunch::ui
