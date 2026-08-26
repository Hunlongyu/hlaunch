#pragma once

#include "platform/windows/window_effects.h"
#include "ui/launcher_layout.h"
#include "ui/theme.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <winrt/base.h>

#include <functional>
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
        core::ThemeMode themeMode,
        QueryChangedHandler queryChangedHandler,
        KeyHandler keyHandler);
    void setThemeMode(core::ThemeMode themeMode);
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
        LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool createDeviceIndependentResources();
    [[nodiscard]] bool createDeviceResources();
    void discardDeviceResources() noexcept;
    void notifyQueryChanged();
    void eraseLastCharacter();
    void pasteClipboardText();
    void render();

    HWND window_{};
    UINT dpi_{96};
    bool translucentSurface_{true};
    core::ThemeMode themeMode_{core::ThemeMode::Dark};
    SearchPopupLayout layout_{};
    std::wstring query_{};
    QueryChangedHandler queryChangedHandler_{};
    KeyHandler keyHandler_{};
    winrt::com_ptr<ID2D1Factory> d2dFactory_{};
    winrt::com_ptr<IDWriteFactory> writeFactory_{};
    winrt::com_ptr<ID2D1HwndRenderTarget> renderTarget_{};
    winrt::com_ptr<IDWriteTextFormat> bodyFormat_{};
    winrt::com_ptr<ID2D1SolidColorBrush> backgroundBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> surfaceBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> textBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> queryTextBrush_{};
    winrt::com_ptr<ID2D1SolidColorBrush> borderBrush_{};
};

} // namespace hlaunch::ui
