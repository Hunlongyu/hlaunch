#pragma once

#include "core/data_model.h"

#include <Windows.h>

#include <expected>
#include <functional>
#include <string>

namespace hlaunch::ui {

class SettingsWindow final {
public:
    using AppearanceChangedHandler = std::function<bool(const core::AppearanceConfig&)>;
    using ActivationChangedHandler = std::function<std::expected<void, std::wstring>(
        const core::ActivationConfig&)>;

    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    [[nodiscard]] bool show(
        HINSTANCE instance,
        HWND owner,
        const core::AppearanceConfig& appearance,
        const core::ActivationConfig& activation,
        AppearanceChangedHandler appearanceChangedHandler,
        ActivationChangedHandler activationChangedHandler);
    void hide();
    void setAppearance(const core::AppearanceConfig& appearance);
    void setActivation(const core::ActivationConfig& activation);

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
    [[nodiscard]] bool applyActivationFromControls();
    void syncControls();
    void syncActivationControls();
    void updateActivationEnabledState();

    HWND window_{};
    HWND themeCombo_{};
    HWND backdropCombo_{};
    HWND opacityEdit_{};
    HWND statusText_{};
    HWND hotkeyEnabledCheck_{};
    HWND altCheck_{};
    HWND controlCheck_{};
    HWND shiftCheck_{};
    HWND winCheck_{};
    HWND hotkeyKeyCombo_{};
    HWND screenEdgeEnabledCheck_{};
    HWND fullscreenCheck_{};
    HWND activationStatusText_{};
    core::AppearanceConfig appearance_{};
    core::ActivationConfig activation_{};
    AppearanceChangedHandler appearanceChangedHandler_{};
    ActivationChangedHandler activationChangedHandler_{};
};

} // namespace hlaunch::ui
