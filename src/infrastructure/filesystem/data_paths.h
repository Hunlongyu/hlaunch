#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace hlaunch::infrastructure::filesystem {

struct DataPathOptions {
    std::filesystem::path executablePath{};
    std::optional<std::filesystem::path> localAppDataOverride{};
    bool forcePortable{};
};

struct DataPaths {
    std::filesystem::path root{};
    std::filesystem::path configFile{};
    std::filesystem::path itemsFile{};
    std::filesystem::path logDirectory{};
    bool portable{};

    bool operator==(const DataPaths&) const = default;
};

struct DataPathError {
    unsigned long systemCode{};
    std::string message{};
};

[[nodiscard]] std::expected<DataPaths, DataPathError> resolveDataPaths(
    const DataPathOptions& options);

} // namespace hlaunch::infrastructure::filesystem
