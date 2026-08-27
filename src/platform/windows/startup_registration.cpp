#include "platform/windows/startup_registration.h"

#include <wil/resource.h>

#include <limits>

namespace hlaunch::platform::windows {

std::wstring buildStartupCommand(
    const std::filesystem::path& executablePath,
    const bool forcePortable)
{
    if (executablePath.empty()) {
        return {};
    }
    std::wstring command{L'"'};
    command.append(executablePath.native());
    command.push_back(L'"');
    if (forcePortable) {
        command.append(L" --portable");
    }
    return command;
}

std::expected<bool, StartupRegistrationError> isStartupEnabled(
    const StartupRegistrationLocation& location)
{
    wil::unique_hkey key{};
    const auto openResult = RegOpenKeyExW(
        location.root,
        location.subkey.c_str(),
        0,
        KEY_QUERY_VALUE,
        key.put());
    if (openResult == ERROR_FILE_NOT_FOUND || openResult == ERROR_PATH_NOT_FOUND) {
        return false;
    }
    if (openResult != ERROR_SUCCESS) {
        return std::unexpected(StartupRegistrationError{
            static_cast<DWORD>(openResult),
        });
    }

    DWORD type{};
    DWORD size{};
    const auto queryResult = RegQueryValueExW(
        key.get(),
        location.valueName.c_str(),
        nullptr,
        &type,
        nullptr,
        &size);
    if (queryResult == ERROR_FILE_NOT_FOUND) {
        return false;
    }
    if (queryResult != ERROR_SUCCESS) {
        return std::unexpected(StartupRegistrationError{
            static_cast<DWORD>(queryResult),
        });
    }
    if (type != REG_SZ || size < sizeof(wchar_t)) {
        return std::unexpected(StartupRegistrationError{ERROR_INVALID_DATA});
    }
    return true;
}

std::expected<void, StartupRegistrationError> setStartupEnabled(
    const std::filesystem::path& executablePath,
    const bool forcePortable,
    const bool enabled,
    const StartupRegistrationLocation& location)
{
    if (!enabled) {
        wil::unique_hkey key{};
        const auto openResult = RegOpenKeyExW(
            location.root,
            location.subkey.c_str(),
            0,
            KEY_SET_VALUE,
            key.put());
        if (openResult == ERROR_FILE_NOT_FOUND || openResult == ERROR_PATH_NOT_FOUND) {
            return {};
        }
        if (openResult != ERROR_SUCCESS) {
            return std::unexpected(StartupRegistrationError{
                static_cast<DWORD>(openResult),
            });
        }
        const auto deleteResult = RegDeleteValueW(key.get(), location.valueName.c_str());
        if (deleteResult != ERROR_SUCCESS && deleteResult != ERROR_FILE_NOT_FOUND) {
            return std::unexpected(StartupRegistrationError{
                static_cast<DWORD>(deleteResult),
            });
        }
        return {};
    }

    const auto command = buildStartupCommand(executablePath, forcePortable);
    if (command.empty()) {
        return std::unexpected(StartupRegistrationError{ERROR_INVALID_PARAMETER});
    }
    const auto byteCount = (command.size() + 1) * sizeof(wchar_t);
    if (byteCount > std::numeric_limits<DWORD>::max()) {
        return std::unexpected(StartupRegistrationError{ERROR_BUFFER_OVERFLOW});
    }

    wil::unique_hkey key{};
    DWORD disposition{};
    const auto createResult = RegCreateKeyExW(
        location.root,
        location.subkey.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        key.put(),
        &disposition);
    if (createResult != ERROR_SUCCESS) {
        return std::unexpected(StartupRegistrationError{
            static_cast<DWORD>(createResult),
        });
    }
    const auto setResult = RegSetValueExW(
        key.get(),
        location.valueName.c_str(),
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>(byteCount));
    if (setResult != ERROR_SUCCESS) {
        return std::unexpected(StartupRegistrationError{
            static_cast<DWORD>(setResult),
        });
    }
    return {};
}

} // namespace hlaunch::platform::windows
