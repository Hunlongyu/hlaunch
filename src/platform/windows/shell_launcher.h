#pragma once

#include "core/data_model.h"

#include <Windows.h>
#include <wil/resource.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace hlaunch::platform::windows {

enum class ShellLaunchErrorCode : std::uint8_t {
    InvalidUtf8,
    LaunchFailed,
    Cancelled,
};

struct ShellLaunchError {
    ShellLaunchErrorCode code{ShellLaunchErrorCode::LaunchFailed};
    DWORD systemCode{};
};

struct ShellLaunchResult {
    wil::unique_process_handle process{};
};

[[nodiscard]] std::wstring quoteWindowsArgument(std::wstring_view argument);
[[nodiscard]] std::expected<std::wstring, ShellLaunchError> buildShellParameterString(
    const std::vector<std::string>& arguments);
[[nodiscard]] std::expected<ShellLaunchResult, ShellLaunchError> launchItem(
    HWND owner,
    const core::LaunchItem& item);

} // namespace hlaunch::platform::windows
