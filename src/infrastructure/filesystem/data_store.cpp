#include "infrastructure/filesystem/data_store.h"

#include <iterator>
#include <string_view>
#include <utility>

namespace hlaunch::infrastructure::filesystem {
namespace {

core::ItemsDocument defaultItemsDocument()
{
    return core::ItemsDocument{
        .tabs = {core::Tab{
            .id = "42f8b39d-18ba-47b0-9dbe-e7836ef69f49",
            .name = "默认",
        }},
    };
}

StoreError fileStoreError(FileError error)
{
    return StoreError{
        .code = StoreErrorCode::FileIo,
        .path = std::move(error.path),
        .systemCode = error.systemCode,
        .message = std::move(error.message),
    };
}

StoreError jsonStoreError(
    const std::filesystem::path& path,
    std::vector<json::JsonIssue> issues)
{
    return StoreError{
        .code = StoreErrorCode::InvalidData,
        .path = path,
        .message = "domain data cannot be encoded",
        .issues = std::move(issues),
    };
}

template <typename Value, typename Decode>
std::expected<DocumentLoadResult<Value>, StoreError> loadDocument(
    const std::filesystem::path& path,
    Value defaults,
    Decode decode)
{
    auto primaryContent = readFile(path, json::maxJsonDocumentBytes);
    if (!primaryContent) {
        if (!isMissingFileError(primaryContent.error())) {
            return std::unexpected(fileStoreError(std::move(primaryContent.error())));
        }

        auto backupContent = readFile(backupPathFor(path), json::maxJsonDocumentBytes);
        if (!backupContent) {
            if (isMissingFileError(backupContent.error())) {
                return DocumentLoadResult<Value>{
                    .value = std::move(defaults),
                    .source = LoadSource::Defaults,
                };
            }
            return std::unexpected(fileStoreError(std::move(backupContent.error())));
        }

        auto decodedBackup = decode(*backupContent);
        return DocumentLoadResult<Value>{
            .value = std::move(decodedBackup.value),
            .source = LoadSource::Backup,
            .issues = std::move(decodedBackup.issues),
            .readOnlyProtection = decodedBackup.readOnlyProtection,
        };
    }

    auto decodedPrimary = decode(*primaryContent);
    if (decodedPrimary.value || decodedPrimary.readOnlyProtection) {
        return DocumentLoadResult<Value>{
            .value = std::move(decodedPrimary.value),
            .source = LoadSource::Primary,
            .issues = std::move(decodedPrimary.issues),
            .readOnlyProtection = decodedPrimary.readOnlyProtection,
        };
    }

    auto primaryIssues = std::move(decodedPrimary.issues);
    auto backupContent = readFile(backupPathFor(path), json::maxJsonDocumentBytes);
    if (!backupContent) {
        if (isMissingFileError(backupContent.error())) {
            return DocumentLoadResult<Value>{
                .source = LoadSource::Primary,
                .issues = std::move(primaryIssues),
            };
        }
        return std::unexpected(fileStoreError(std::move(backupContent.error())));
    }

    auto decodedBackup = decode(*backupContent);
    primaryIssues.insert(
        primaryIssues.end(),
        std::make_move_iterator(decodedBackup.issues.begin()),
        std::make_move_iterator(decodedBackup.issues.end()));
    return DocumentLoadResult<Value>{
        .value = std::move(decodedBackup.value),
        .source = LoadSource::Backup,
        .issues = std::move(primaryIssues),
        .readOnlyProtection = decodedBackup.readOnlyProtection,
    };
}

template <typename Value, typename Encode>
std::expected<void, StoreError> saveDocument(
    const std::filesystem::path& path,
    const Value& value,
    Encode encode)
{
    auto encoded = encode(value);
    if (!encoded) {
        return std::unexpected(jsonStoreError(path, std::move(encoded.error())));
    }
    auto writeResult = writeFileAtomically(path, *encoded);
    if (!writeResult) {
        return std::unexpected(fileStoreError(std::move(writeResult.error())));
    }
    return {};
}

} // namespace

std::expected<ConfigLoadResult, StoreError> loadConfig(const std::filesystem::path& path)
{
    return loadDocument(
        path,
        core::ApplicationConfig{},
        [](const std::string_view input) { return json::decodeConfig(input); });
}

std::expected<ItemsLoadResult, StoreError> loadItems(const std::filesystem::path& path)
{
    return loadDocument(
        path,
        defaultItemsDocument(),
        [](const std::string_view input) { return json::decodeItems(input); });
}

std::expected<void, StoreError> saveConfig(
    const std::filesystem::path& path,
    const core::ApplicationConfig& config)
{
    return saveDocument(
        path,
        config,
        [](const core::ApplicationConfig& value) { return json::encodeConfig(value); });
}

std::expected<void, StoreError> saveItems(
    const std::filesystem::path& path,
    const core::ItemsDocument& items)
{
    return saveDocument(
        path,
        items,
        [](const core::ItemsDocument& value) { return json::encodeItems(value); });
}

} // namespace hlaunch::infrastructure::filesystem
