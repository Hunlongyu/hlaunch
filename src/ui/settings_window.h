#pragma once

#include "core/data_model.h"
#include "ui/dpi_layout.h"
#include "ui/system_appearance.h"

#include <Windows.h>

#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace hlaunch::ui {

class SettingsWindow final {
public:
    using AppearanceChangedHandler =
        std::function<bool(const core::AppearanceConfig&)>;
    using ActivationChangedHandler =
        std::function<std::expected<void, std::wstring>(const core::ActivationConfig&)>;
    using StartupChangedHandler = std::function<std::expected<void, std::wstring>(bool)>;
    using DiagnosticsChangedHandler = std::function<std::expected<void, std::wstring>(bool)>;

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
        bool diagnosticLoggingEnabled,
        AppearanceChangedHandler appearanceChangedHandler,
        ActivationChangedHandler activationChangedHandler,
        StartupChangedHandler startupChangedHandler,
        DiagnosticsChangedHandler diagnosticsChangedHandler);
    void hide();
    void setAppearance(const core::AppearanceConfig& appearance);
    void setActivation(const core::ActivationConfig& activation);
    void setActivationStatus(const wchar_t* message);
    void setStatus(const wchar_t* message);
    void setDiagnosticLogging(bool enabled, const wchar_t* status);
    void refreshSystemAppearance();

    [[nodiscard]] HWND handle() const noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

private:
    static INT_PTR CALLBACK dialogProcedure(
        HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    INT_PTR handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool create(HINSTANCE instance, HWND owner);
    void createControls();
    void positionOverOwner(HWND owner);
    [[nodiscard]] bool applyAllFromControls();
    [[nodiscard]] bool applyAppearanceFromControls();
    [[nodiscard]] bool applyActivationFromControls();
    [[nodiscard]] bool applyStartupFromControls();
    [[nodiscard]] bool applyDiagnosticsFromControls();
    void syncAppearanceControls();
    void syncActivationControls();
    void syncStartupControls();
    void syncDiagnosticsControls();
    void updateActivationEnabledState();
    void updateVisiblePage();

    HWND window_{};
    HWND tabControl_{};
    HWND tabPageBackground_{};
    HWND backdropCombo_{};
    HWND opacitySlider_{};
    HWND opacityEdit_{};
    HWND settingsStatusText_{};
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
    HWND processBlocklistEdit_{};
    HWND processAllowlistEdit_{};
    HWND activationStatusText_{};
    HWND startupEnabledCheck_{};
    HWND startupStatusText_{};
    HWND diagnosticLoggingEnabledCheck_{};
    HWND diagnosticsStatusText_{};
    SystemUiFont systemUiFont_{};
    core::AppearanceConfig appearance_{};
    core::ActivationConfig activation_{};
    bool startupEnabled_{};
    bool diagnosticLoggingEnabled_{true};
    std::wstring startupLoadError_{};
    AppearanceChangedHandler appearanceChangedHandler_{};
    ActivationChangedHandler activationChangedHandler_{};
    StartupChangedHandler startupChangedHandler_{};
    DiagnosticsChangedHandler diagnosticsChangedHandler_{};
    std::vector<DialogControlLayout> controlLayouts_{};
    std::vector<HWND> generalPageControls_{};
    std::vector<HWND> activationPageControls_{};
    std::vector<HWND> pageOverlayControls_{};
};

} // namespace hlaunch::ui
