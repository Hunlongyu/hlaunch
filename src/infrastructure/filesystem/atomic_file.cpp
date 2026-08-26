#include "infrastructure/filesystem/atomic_file.h"

#include <Windows.h>
#include <wil/resource.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <system_error>
#include <utility>

namespace hlaunch::infrastructure::filesystem {
namespace {

FileError makeError(
    const FileOperation operation,
    std::filesystem::path path,
    const unsigned long code,
    std::string message)
{
    return FileError{operation, std::move(path), code, std::move(message)};
}

void removeTemporaryFile(const std::filesystem::path& path) noexcept
{
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        DeleteFileW(path.c_str());
    }
}

std::expected<void, FileError> writeAll(
    const HANDLE file,
    const std::filesystem::path& path,
    const std::string_view content)
{
    std::size_t offset = 0;
    while (offset < content.size()) {
        const auto remaining = content.size() - offset;
        const auto chunkSize = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            std::numeric_limits<DWORD>::max()));
        DWORD bytesWritten = 0;
        if (!WriteFile(file, content.data() + offset, chunkSize, &bytesWritten, nullptr)) {
            return std::unexpected(makeError(
                FileOperation::Write,
                path,
                GetLastError(),
                "failed to write temporary file"));
        }
        if (bytesWritten == 0) {
            return std::unexpected(makeError(
                FileOperation::Write,
                path,
                ERROR_WRITE_FAULT,
                "temporary file write made no progress"));
        }
        offset += bytesWritten;
    }
    return {};
}

} // namespace

std::filesystem::path backupPathFor(const std::filesystem::path& path)
{
    auto result = path;
    result += L".bak";
    return result;
}

std::filesystem::path temporaryPathFor(const std::filesystem::path& path)
{
    auto result = path;
    result += L".tmp";
    return result;
}

bool isMissingFileError(const FileError& error) noexcept
{
    return error.systemCode == ERROR_FILE_NOT_FOUND
        || error.systemCode == ERROR_PATH_NOT_FOUND;
}

std::expected<std::string, FileError> readFile(
    const std::filesystem::path& path,
    const std::size_t maximumBytes)
{
    wil::unique_hfile file{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr)};
    if (!file) {
        return std::unexpected(makeError(
            FileOperation::Open,
            path,
            GetLastError(),
            "failed to open file for reading"));
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file.get(), &fileSize)) {
        return std::unexpected(makeError(
            FileOperation::Inspect,
            path,
            GetLastError(),
            "failed to inspect file size"));
    }
    if (fileSize.QuadPart < 0
        || static_cast<unsigned long long>(fileSize.QuadPart) > maximumBytes) {
        return std::unexpected(makeError(
            FileOperation::Inspect,
            path,
            ERROR_FILE_TOO_LARGE,
            "file exceeds the configured size limit"));
    }

    std::string content(static_cast<std::size_t>(fileSize.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < content.size()) {
        const auto remaining = content.size() - offset;
        const auto chunkSize = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            std::numeric_limits<DWORD>::max()));
        DWORD bytesRead = 0;
        if (!ReadFile(file.get(), content.data() + offset, chunkSize, &bytesRead, nullptr)) {
            return std::unexpected(makeError(
                FileOperation::Read,
                path,
                GetLastError(),
                "failed to read file"));
        }
        if (bytesRead == 0) {
            return std::unexpected(makeError(
                FileOperation::Read,
                path,
                ERROR_HANDLE_EOF,
                "file ended before the advertised size"));
        }
        offset += bytesRead;
    }
    return content;
}

std::expected<void, FileError> writeFileAtomically(
    const std::filesystem::path& path,
    const std::string_view content)
{
    if (path.empty() || path.parent_path().empty()) {
        return std::unexpected(makeError(
            FileOperation::Inspect,
            path,
            ERROR_INVALID_PARAMETER,
            "target file must have a parent directory"));
    }

    std::error_code directoryError{};
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
        return std::unexpected(makeError(
            FileOperation::CreateDirectories,
            path.parent_path(),
            static_cast<unsigned long>(directoryError.value()),
            "failed to create the data directory"));
    }

    const auto temporaryPath = temporaryPathFor(path);
    wil::unique_hfile temporaryFile{CreateFileW(
        temporaryPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
        nullptr)};
    if (!temporaryFile) {
        return std::unexpected(makeError(
            FileOperation::Open,
            temporaryPath,
            GetLastError(),
            "failed to create the temporary file"));
    }

    if (auto writeResult = writeAll(temporaryFile.get(), temporaryPath, content); !writeResult) {
        temporaryFile.reset();
        removeTemporaryFile(temporaryPath);
        return std::unexpected(std::move(writeResult.error()));
    }
    if (!FlushFileBuffers(temporaryFile.get())) {
        const auto code = GetLastError();
        temporaryFile.reset();
        removeTemporaryFile(temporaryPath);
        return std::unexpected(makeError(
            FileOperation::Flush,
            temporaryPath,
            code,
            "failed to flush the temporary file"));
    }
    temporaryFile.reset();

    const auto targetAttributes = GetFileAttributesW(path.c_str());
    if (targetAttributes == INVALID_FILE_ATTRIBUTES) {
        const auto code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
            removeTemporaryFile(temporaryPath);
            return std::unexpected(makeError(
                FileOperation::Inspect,
                path,
                code,
                "failed to inspect the target file"));
        }
        if (!MoveFileExW(
                temporaryPath.c_str(),
                path.c_str(),
                MOVEFILE_WRITE_THROUGH)) {
            const auto moveCode = GetLastError();
            removeTemporaryFile(temporaryPath);
            return std::unexpected(makeError(
                FileOperation::Replace,
                path,
                moveCode,
                "failed to move the first version into place"));
        }
        return {};
    }
    if ((targetAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        removeTemporaryFile(temporaryPath);
        return std::unexpected(makeError(
            FileOperation::Replace,
            path,
            ERROR_DIRECTORY,
            "target path is a directory"));
    }

    const auto backupPath = backupPathFor(path);
    const auto backupAttributes = GetFileAttributesW(backupPath.c_str());
    if (backupAttributes != INVALID_FILE_ATTRIBUTES) {
        if ((backupAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            removeTemporaryFile(temporaryPath);
            return std::unexpected(makeError(
                FileOperation::Replace,
                backupPath,
                ERROR_DIRECTORY,
                "backup path is a directory"));
        }
        if (!DeleteFileW(backupPath.c_str())) {
            const auto deleteCode = GetLastError();
            removeTemporaryFile(temporaryPath);
            return std::unexpected(makeError(
                FileOperation::Replace,
                backupPath,
                deleteCode,
                "failed to remove the previous backup"));
        }
    }
    else {
        const auto backupInspectCode = GetLastError();
        if (backupInspectCode != ERROR_FILE_NOT_FOUND
            && backupInspectCode != ERROR_PATH_NOT_FOUND) {
            removeTemporaryFile(temporaryPath);
            return std::unexpected(makeError(
                FileOperation::Inspect,
                backupPath,
                backupInspectCode,
                "failed to inspect the backup file"));
        }
    }

    if (!ReplaceFileW(
            path.c_str(),
            temporaryPath.c_str(),
            backupPath.c_str(),
            REPLACEFILE_WRITE_THROUGH,
            nullptr,
            nullptr)) {
        const auto replaceCode = GetLastError();
        removeTemporaryFile(temporaryPath);
        return std::unexpected(makeError(
            FileOperation::Replace,
            path,
            replaceCode,
            "failed to atomically replace the target file"));
    }
    return {};
}

} // namespace hlaunch::infrastructure::filesystem
