#pragma once

#include "core/data_model.h"

#include <Windows.h>

#include <functional>

namespace hlaunch::ui {

class SettingsWindow final {
public:
    using AppearanceChangedHandler = std::function<bool(const core::AppearanceConfig&)>;

    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    [[nodiscard]] bool show(
        HINSTANCE instance,
        HWND owner,
        const core::AppearanceConfig& appearance,
        AppearanceChangedHandler appearanceChangedHandler);
    void hide();
    void setAppearance(const core::AppearanceConfig& appearance);

    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

private:
    static INT_PTR CALLBACK dialogProcedure(
        HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool create(HINSTANCE instance, HWND owner);
    void createControls();
    void positionOverOwner(HWND owner);
    [[nodiscard]] bool applyAppearanceFromControls(bool includeOpacity);
    void syncControls();

    HWND window_{};
    HWND themeCombo_{};
    HWND backdropCombo_{};
    HWND opacityEdit_{};
    HWND statusText_{};
    core::AppearanceConfig appearance_{};
    AppearanceChangedHandler appearanceChangedHandler_{};
};

} // namespace hlaunch::ui
