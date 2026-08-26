#pragma once

#include "activation/activation_context.h"
#include "core/data_model.h"
#include "platform/windows/window_effects.h"
#include "ui/search_window.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <winrt/base.h>

#include <functional>
#include <string_view>

namespace hlaunch::ui {

class LauncherWindow final {
public:
    using LaunchHandler = std::function<void(const core::LaunchItem&)>;

    LauncherWindow() = default;
    ~LauncherWindow();

    LauncherWindow(const LauncherWindow&) = delete;
    LauncherWindow& operator=(const LauncherWindow&) = delete;

    [[nodiscard]] bool create(
        HINSTANCE instance,
        const platform::windows::WindowEffects& effects,
        bool showSearch,
        core::ItemsDocument document,
        LaunchHandler launchHandler);
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
    [[nodiscard]] bool createDeviceResources();
    void discardDeviceResources() noexcept;
    void positionOnCursorMonitor();
    void positionOnScreenEdge(const activation::ScreenEdgeHit& hit);
    void positionSearchWindow();
    void render();
    [[nodiscard]] bool handleKeyDown(WPARAM key);
    void activateFocusedItem();
    void changeActiveTab(std::size_t tabIndex);
    [[nodiscard]] const core::Tab* activeTab() const noexcept;
    [[nodiscard]] std::size_t visibleItemCount() const noexcept;
    [[nodiscard]] std::size_t displayedTileCount() const noexcept;
    void drawText(
        std::wstring_view text,
        const D2D1_RECT_F& bounds,
        IDWriteTextFormat* format,
        ID2D1Brush* brush);

    HWND window_{};
    UINT dpi_{96};
    bool translucentSurface_{true};
    bool searchVisible_{};
    core::ItemsDocument document_{};
    std::size_t activeTabIndex_{};
    std::size_t focusedItemIndex_{};
    bool windowFocused_{};
    LaunchHandler launchHandler_{};
    SearchWindow searchWindow_{};
    winrt::com_ptr<ID2D1Factory> d2dFactory_{};
    winrt::com_ptr<IDWriteFactory> writeFactory_{};
    winrt::com_ptr<ID2D1HwndRenderTarget> renderTarget_{};
    winrt::com_ptr<IDWriteTextFormat> titleFormat_{};
    winrt::com_ptr<IDWriteTextFormat> bodyFormat_{};
    winrt::com_ptr<IDWriteTextFormat> smallFormat_{};
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
