#pragma once

#include "platform/windows/activation_command.h"
#include "platform/windows/window_effects.h"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlaunch::app {

struct StartupOptions {
    bool portable{};
    bool showSearch{};
    std::optional<platform::windows::ActivationCommand> activation{};
    platform::windows::WindowEffects windowEffects{};
};

[[nodiscard]] std::expected<StartupOptions, std::wstring> parseCommandLine(
    std::span<const std::wstring_view> arguments);

} // namespace hlaunch::app
