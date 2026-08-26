#include "infrastructure/filesystem/data_paths.h"

#include <ShlObj.h>
#include <wil/resource.h>

#include <system_error>

namespace hlaunch::infrastructure::filesystem {
namespace {

DataPathError makeError(const unsigned long code, std::string message)
{
    return DataPathError{code, std::move(message)};
}

} // namespace

std::expected<DataPaths, DataPathError> resolveDataPaths(const DataPathOptions& options)
{
    if (options.executablePath.empty()) {
        return std::unexpected(makeError(ERROR_INVALID_PARAMETER, "executable path is empty"));
    }

    const auto executableDirectory = options.executablePath.parent_path();
    const auto portableFlag = executableDirectory / L"portable.flag";
    const auto portableFlagAttributes = GetFileAttributesW(portableFlag.c_str());
    bool portableFlagExists = false;
    if (portableFlagAttributes == INVALID_FILE_ATTRIBUTES) {
        const auto code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
            return std::unexpected(makeError(code, "failed to inspect portable.flag"));
        }
    }
    else {
        portableFlagExists = (portableFlagAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    const bool portable = options.forcePortable || portableFlagExists;
    std::filesystem::path root{};
    if (portable) {
        root = executableDirectory / L"data";
    }
    else if (options.localAppDataOverride) {
        root = *options.localAppDataOverride / L"HLaunch";
    }
    else {
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
        root = std::filesystem::path{localAppData.get()} / L"HLaunch";
    }

    return DataPaths{
        .root = root,
        .configFile = root / L"config.json",
        .itemsFile = root / L"items.json",
        .logDirectory = root / L"logs",
        .portable = portable,
    };
}

} // namespace hlaunch::infrastructure::filesystem
