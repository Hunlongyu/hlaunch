#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace hlaunch::infrastructure::filesystem {

enum class FileOperation {
    Inspect,
    CreateDirectories,
    Open,
    Read,
    Write,
    Flush,
    Replace,
};

struct FileError {
    FileOperation operation{FileOperation::Inspect};
    std::filesystem::path path{};
    unsigned long systemCode{};
    std::string message{};
};

[[nodiscard]] std::filesystem::path backupPathFor(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path temporaryPathFor(const std::filesystem::path& path);
[[nodiscard]] bool isMissingFileError(const FileError& error) noexcept;

[[nodiscard]] std::expected<std::string, FileError> readFile(
    const std::filesystem::path& path,
    std::size_t maximumBytes);
[[nodiscard]] std::expected<void, FileError> writeFileAtomically(
    const std::filesystem::path& path,
    std::string_view content);

} // namespace hlaunch::infrastructure::filesystem
