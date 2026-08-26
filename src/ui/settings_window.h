#pragma once

#include "core/data_model.h"

#include <Windows.h>

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
    static INT_PTR CALLBACK dialogProcedure(
        HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool create(HINSTANCE instance, HWND owner);
    void createControls();
    void positionOverOwner(HWND owner);

    HWND window_{};
    HWND themeCombo_{};
    core::ThemeMode themeMode_{core::ThemeMode::Dark};
    ThemeChangedHandler themeChangedHandler_{};
};

} // namespace hlaunch::ui
