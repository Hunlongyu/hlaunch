#pragma once

#include <Windows.h>

#include <expected>
#include <filesystem>
#include <string>

namespace hlaunch::platform::windows {

struct StartupRegistrationLocation {
    // Empty selects HLaunch.Logon.<current-user SID>; tests use a unique name.
    std::wstring taskName{};
    HKEY root{HKEY_CURRENT_USER};
    std::wstring subkey{L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"};
    std::wstring valueName{L"HLaunch"};
    std::wstring approvalSubkey{
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"};
};

struct StartupRegistrationError {
    DWORD systemCode{};
};

[[nodiscard]] std::wstring buildStartupCommand(
    const std::filesystem::path& executablePath,
    bool forcePortable);
[[nodiscard]] std::expected<bool, StartupRegistrationError> isStartupEnabled(
    const std::filesystem::path& executablePath,
    const StartupRegistrationLocation& location = {});
// Call on a COM-initialized background thread. Only migrates a legacy Run value
// for this executable; an existing task is never silently re-enabled.
[[nodiscard]] std::expected<void, StartupRegistrationError> migrateStartupRegistration(
    const std::filesystem::path& executablePath,
    bool forcePortable,
    const StartupRegistrationLocation& location = {});
[[nodiscard]] std::expected<void, StartupRegistrationError> setStartupEnabled(
    const std::filesystem::path& executablePath,
    bool forcePortable,
    bool enabled,
    const StartupRegistrationLocation& location = {});

} // namespace hlaunch::platform::windows
