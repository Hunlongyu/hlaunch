#pragma once

#include "core/data_model.h"

#include <Windows.h>

#include <cstdint>
#include <expected>
#include <filesystem>

namespace hlaunch::platform::windows {

struct ItemPathContext
{
    std::filesystem::path executableDirectory{};
    bool portable{};
};

enum class ItemPathErrorCode : std::uint8_t
{
    InvalidUtf8,
    InvalidContext,
    InvalidPath,
    ExpansionFailed,
};

struct ItemPathError
{
    ItemPathErrorCode code{ItemPathErrorCode::InvalidContext};
    DWORD systemCode{};
};

[[nodiscard]] std::expected<core::LaunchItem, ItemPathError> resolveItemPaths(
    const core::LaunchItem &item,
    const ItemPathContext &context);

[[nodiscard]] std::expected<core::LaunchItem, ItemPathError> makeItemPathsPortable(
    const core::LaunchItem &item,
    const ItemPathContext &context);

} // namespace hlaunch::platform::windows
