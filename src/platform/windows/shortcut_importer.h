#pragma once

#include "core/data_model.h"

#include <filesystem>
#include <optional>

namespace hlaunch::platform::windows {

// Imports a snapshot of the launch properties, or retains a private Shell link.
[[nodiscard]] std::optional<core::LaunchItem> importShortcut(
    core::LaunchItem item,
    const std::filesystem::path& source,
    const std::filesystem::path& shortcutDirectory);

} // namespace hlaunch::platform::windows
