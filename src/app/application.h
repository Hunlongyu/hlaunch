#pragma once

#include "app/command_line.h"
#include "core/data_model.h"
#include "infrastructure/filesystem/items_save_worker.h"
#include "platform/windows/global_hotkey.h"
#include "platform/windows/screen_edge_activation.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/tray_icon.h"
#include "ui/launcher_window.h"
#include "ui/settings_window.h"

#include <Windows.h>

#include <optional>
#include <filesystem>

namespace hlaunch::app {

class Application final {
public:
    [[nodiscard]] int run(HINSTANCE instance, const StartupOptions& options);

private:
    static LRESULT CALLBACK activationWindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);
    LRESULT handleActivationMessage(UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool createActivationWindow(HINSTANCE instance);
    void execute(platform::windows::ActivationCommand command);
    void launch(const core::LaunchItem& item);
    void showSettings();
    [[nodiscard]] bool changeTheme(core::ThemeMode themeMode);

    HINSTANCE instance_{};
    core::ApplicationConfig config_{};
    std::filesystem::path configFile_{};
    std::optional<platform::windows::SingleInstance> singleInstance_{};
    std::optional<infrastructure::filesystem::ItemsSaveWorker> itemsSaver_{};
    ui::LauncherWindow launcher_{};
    ui::SettingsWindow settings_{};
    HWND activationWindow_{};
    platform::windows::GlobalHotkey hotkey_{};
    platform::windows::ScreenEdgeActivation screenEdge_{};
    platform::windows::TrayIcon trayIcon_{};
};

} // namespace hlaunch::app
