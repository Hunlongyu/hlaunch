#pragma once

#include "core/data_model.h"
#include "ui/theme.h"

#include <Windows.h>
#include <wil/resource.h>

#include <functional>

namespace hlaunch::ui {

class SettingsWindow final {
public:
    using ThemeChangedHandler = std::function<bool(core::ThemeMode)>;

    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    [[nodiscard]] bool show(
        HINSTANCE instance,
        HWND owner,
        core::ThemeMode themeMode,
        ThemeChangedHandler themeChangedHandler);
    void hide();
    void setThemeMode(core::ThemeMode themeMode);

    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool create(HINSTANCE instance, HWND owner);
    void createControls();
    void rebuildThemeResources();
    void paint();
    void drawButton(const DRAWITEMSTRUCT& item) const;
    void drawComboItem(const DRAWITEMSTRUCT& item) const;
    [[nodiscard]] bool closeHit(POINT point) const noexcept;
    void positionOverOwner(HWND owner);

    HWND window_{};
    HWND themeCombo_{};
    core::ThemeMode themeMode_{core::ThemeMode::Dark};
    ThemePalette palette_{};
    ThemeChangedHandler themeChangedHandler_{};
    wil::unique_hbrush backgroundBrush_{};
    wil::unique_hbrush surfaceBrush_{};
};

} // namespace hlaunch::ui
