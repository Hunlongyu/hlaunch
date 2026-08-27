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
    using StartupChangedHandler = std::function<std::expected<void, std::wstring>(bool)>;

    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    [[nodiscard]] bool show(
        HINSTANCE instance,
        HWND owner,
        const core::AppearanceConfig& appearance,
        const core::ActivationConfig& activation,
        std::expected<bool, std::wstring> startupEnabled,
        AppearanceChangedHandler appearanceChangedHandler,
        ActivationChangedHandler activationChangedHandler,
        StartupChangedHandler startupChangedHandler);
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
    [[nodiscard]] bool applyStartupFromControls();
    void syncControls();
    void syncActivationControls();
    void syncStartupControls();
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
    HWND leftZoneCheck_{};
    HWND rightZoneCheck_{};
    HWND topZoneCheck_{};
    HWND bottomZoneCheck_{};
    HWND topLeftZoneCheck_{};
    HWND topRightZoneCheck_{};
    HWND bottomLeftZoneCheck_{};
    HWND bottomRightZoneCheck_{};
    HWND edgeModeCombo_{};
    HWND thicknessEdit_{};
    HWND cornerSizeEdit_{};
    HWND dwellEdit_{};
    HWND pollEdit_{};
    HWND cooldownEdit_{};
    HWND fullscreenCheck_{};
    HWND activationStatusText_{};
    HWND startupEnabledCheck_{};
    HWND startupApplyButton_{};
    HWND startupStatusText_{};
    core::AppearanceConfig appearance_{};
    core::ActivationConfig activation_{};
    bool startupEnabled_{};
    std::wstring startupLoadError_{};
    AppearanceChangedHandler appearanceChangedHandler_{};
    ActivationChangedHandler activationChangedHandler_{};
    StartupChangedHandler startupChangedHandler_{};
};

} // namespace hlaunch::ui
