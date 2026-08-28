#include "infrastructure/filesystem/data_paths.h"

#include <ShlObj.h>
#include <wil/resource.h>

#include <array>
#include <string_view>
#include <system_error>

namespace hlaunch::infrastructure::filesystem {
namespace {

DataPathError makeError(const unsigned long code, std::string message)
{
    return DataPathError{code, std::move(message)};
}

std::expected<void, DataPathError> verifyExistingDataFile(
    const std::filesystem::path& path)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return {};
        }
        return std::unexpected(makeError(code, "failed to inspect an existing data file"));
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        return std::unexpected(makeError(
            ERROR_DIRECTORY,
            "a data file path resolves to a directory"));
    }

    wil::unique_hfile file{CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr)};
    if (!file) {
        return std::unexpected(makeError(
            GetLastError(),
            "an existing data file is not readable and writable"));
    }
    return {};
}

std::expected<void, DataPathError> prepareDataRoot(
    const std::filesystem::path& root)
{
    std::error_code directoryError{};
    std::filesystem::create_directories(root, directoryError);
    if (directoryError) {
        return std::unexpected(makeError(
            static_cast<unsigned long>(directoryError.value()),
            "failed to create the data directory"));
    }

    const auto attributes = GetFileAttributesW(root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return std::unexpected(makeError(
            GetLastError(),
            "failed to inspect the data directory"));
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
        return std::unexpected(makeError(
            ERROR_DIRECTORY,
            "the data root is not a directory"));
    }

    constexpr std::array<std::wstring_view, 2> persistentFiles{
        L"config.json",
        L"items.json",
    };
    for (const auto fileName : persistentFiles) {
        if (auto result = verifyExistingDataFile(root / fileName); !result) {
            return result;
        }
    }

    const auto probeName = L".hlaunch-write-probe-"
        + std::to_wstring(GetCurrentProcessId()) + L"-"
        + std::to_wstring(GetCurrentThreadId()) + L"-"
        + std::to_wstring(GetTickCount64()) + L".tmp";
    const auto probePath = root / probeName;
    wil::unique_hfile probe{CreateFileW(
        probePath.c_str(),
        GENERIC_READ | GENERIC_WRITE | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr)};
    if (!probe) {
        return std::unexpected(makeError(
            GetLastError(),
            "the data directory is not writable"));
    }

    constexpr char marker = 'H';
    DWORD bytesWritten{};
    if (!WriteFile(probe.get(), &marker, 1U, &bytesWritten, nullptr)) {
        return std::unexpected(makeError(
            GetLastError(),
            "failed to write the data directory probe"));
    }
    if (bytesWritten != 1U) {
        return std::unexpected(makeError(
            ERROR_WRITE_FAULT,
            "failed to write the complete data directory probe"));
    }
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(probe.get(), beginning, nullptr, FILE_BEGIN)) {
        return std::unexpected(makeError(
            GetLastError(),
            "failed to rewind the data directory probe"));
    }
    char readBack{};
    DWORD bytesRead{};
    if (!ReadFile(probe.get(), &readBack, 1U, &bytesRead, nullptr)) {
        return std::unexpected(makeError(
            GetLastError(),
            "failed to read the data directory probe"));
    }
    if (bytesRead != 1U || readBack != marker) {
        return std::unexpected(makeError(
            ERROR_READ_FAULT,
            "failed to read the complete data directory probe"));
    }
    return {};
}

std::expected<std::filesystem::path, DataPathError> localAppDataRoot(
    const DataPathOptions& options)
{
    if (options.localAppDataOverride) {
        return *options.localAppDataOverride / L"HLaunch";
    }

    wil::unique_cotaskmem_string localAppData{};
    const auto result = SHGetKnownFolderPath(
        FOLDERID_LocalAppData,
        KF_FLAG_DEFAULT,
        nullptr,
        localAppData.put());
    if (FAILED(result)) {
        return std::unexpected(makeError(
            static_cast<unsigned long>(result),
            "failed to resolve LocalAppData"));
    }
    return std::filesystem::path{localAppData.get()} / L"HLaunch";
}

DataPaths makeDataPaths(std::filesystem::path root, const bool portable)
{
    return {
        .root = root,
        .configFile = root / L"config.json",
        .itemsFile = root / L"items.json",
        .logDirectory = root / L"logs",
        .iconCacheDirectory = root / L"cache" / L"icons",
        .portable = portable,
    };
}

} // namespace

std::expected<DataPaths, DataPathError> resolveDataPaths(const DataPathOptions& options)
{
    if (options.executablePath.empty()) {
        return std::unexpected(makeError(ERROR_INVALID_PARAMETER, "executable path is empty"));
    }

    const auto portableRoot = options.executablePath.parent_path() / L"data";
    if (auto portableResult = prepareDataRoot(portableRoot); portableResult) {
        return makeDataPaths(portableRoot, true);
    }

    const auto fallbackRoot = localAppDataRoot(options);
    if (!fallbackRoot) {
        return std::unexpected(std::move(fallbackRoot.error()));
    }
    if (auto fallbackResult = prepareDataRoot(*fallbackRoot); !fallbackResult) {
        return std::unexpected(DataPathError{
            fallbackResult.error().systemCode,
            "portable data root unavailable and LocalAppData fallback failed: "
                + fallbackResult.error().message,
        });
    }
    return makeDataPaths(*fallbackRoot, false);
}

} // namespace hlaunch::infrastructure::filesystem
