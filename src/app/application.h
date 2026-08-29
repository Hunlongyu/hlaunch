#pragma once

#include "app/command_line.h"
#include "core/data_model.h"
#include "infrastructure/filesystem/config_save_worker.h"
#include "infrastructure/filesystem/items_save_worker.h"
#include "platform/windows/global_hotkey.h"
#include "platform/windows/screen_edge_activation.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/tray_icon.h"
#include "ui/launcher_window.h"
#include "ui/settings_window.h"

#include <Windows.h>

#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace hlaunch::app {

class Application final {
public:
    [[nodiscard]] int run(HINSTANCE instance, const StartupOptions& options);

private:
    static LRESULT CALLBACK activationWindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) noexcept;
    LRESULT handleActivationMessage(UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool createActivationWindow(HINSTANCE instance);
    void execute(platform::windows::ActivationCommand command);
    void launch(const core::LaunchItem& item);
    void showSettings();
    void toggleStartup();
    [[nodiscard]] bool changeAppearance(const core::AppearanceConfig& appearance);
    [[nodiscard]] std::expected<void, std::wstring> changeActivation(
        const core::ActivationConfig& activation);
    [[nodiscard]] std::expected<void, std::wstring> changeDiagnostics(bool enabled);
    [[nodiscard]] std::expected<void, std::wstring> changeStartup(bool enabled);
    [[nodiscard]] bool applyDiagnosticLogging(bool enabled);
    [[nodiscard]] bool submitConfigSnapshot(core::ApplicationConfig snapshot);
    void handleConfigSaveCompletions();
    void shutdownSaveWorkersForSessionEnd() noexcept;

    HINSTANCE instance_{};
    core::ApplicationConfig config_{};
    core::ApplicationConfig persistedConfig_{};
    std::filesystem::path executablePath_{};
    std::filesystem::path logDirectory_{};
    bool forcePortable_{};
    bool diagnosticLoggingActive_{};
    platform::windows::WindowEffects windowEffects_{};
    std::optional<platform::windows::SingleInstance> singleInstance_{};
    std::optional<infrastructure::filesystem::ConfigSaveWorker> configSaver_{};
    std::optional<infrastructure::filesystem::ItemsSaveWorker> itemsSaver_{};
    std::mutex configSaveCompletionMutex_{};
    std::deque<infrastructure::filesystem::ConfigSaveCompletion>
        configSaveCompletions_{};
    std::uint64_t latestConfigRevision_{};
    std::uint64_t persistedConfigRevision_{};
    ui::LauncherWindow launcher_{};
    ui::SettingsWindow settings_{};
    HWND activationWindow_{};
    platform::windows::GlobalHotkey hotkey_{};
    platform::windows::ScreenEdgeActivation screenEdge_{};
    platform::windows::TrayIcon trayIcon_{};
};

} // namespace hlaunch::app
