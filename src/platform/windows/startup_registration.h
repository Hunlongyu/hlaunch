#pragma once

#include <Windows.h>

#include <expected>
#include <filesystem>
#include <string>

namespace hlaunch::platform::windows {

struct StartupRegistrationLocation {
    HKEY root{HKEY_CURRENT_USER};
    std::wstring subkey{L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"};
    std::wstring valueName{L"HLaunch"};
};

struct StartupRegistrationError {
    DWORD systemCode{};
};

[[nodiscard]] std::wstring buildStartupCommand(
    const std::filesystem::path& executablePath,
    bool forcePortable);
[[nodiscard]] std::expected<bool, StartupRegistrationError> isStartupEnabled(
    const StartupRegistrationLocation& location = {});
[[nodiscard]] std::expected<void, StartupRegistrationError> setStartupEnabled(
    const std::filesystem::path& executablePath,
    bool forcePortable,
    bool enabled,
    const StartupRegistrationLocation& location = {});

} // namespace hlaunch::platform::windows
