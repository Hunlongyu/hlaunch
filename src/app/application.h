#pragma once

#include "app/command_line.h"
#include "platform/windows/global_hotkey.h"
#include "platform/windows/single_instance.h"
#include "ui/launcher_window.h"

#include <Windows.h>

#include <optional>

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

    std::optional<platform::windows::SingleInstance> singleInstance_{};
    ui::LauncherWindow launcher_{};
    HWND activationWindow_{};
    platform::windows::GlobalHotkey hotkey_{};
};

} // namespace hlaunch::app
