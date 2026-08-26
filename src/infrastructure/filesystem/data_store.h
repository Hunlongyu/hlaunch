#pragma once

#include "core/data_model.h"
#include "infrastructure/filesystem/atomic_file.h"
#include "infrastructure/json/data_json_codec.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hlaunch::infrastructure::filesystem {

enum class LoadSource {
    Defaults,
    Primary,
    Backup,
};

template <typename Value>
struct DocumentLoadResult {
    std::optional<Value> value{};
    LoadSource source{LoadSource::Defaults};
    std::vector<json::JsonIssue> issues{};
    bool readOnlyProtection{};

    [[nodiscard]] bool recoveredFromBackup() const noexcept
    {
        return source == LoadSource::Backup && value.has_value();
    }
};

enum class StoreErrorCode {
    FileIo,
    InvalidData,
};

struct StoreError {
    StoreErrorCode code{StoreErrorCode::FileIo};
    std::filesystem::path path{};
    unsigned long systemCode{};
    std::string message{};
    std::vector<json::JsonIssue> issues{};
};

using ConfigLoadResult = DocumentLoadResult<core::ApplicationConfig>;
using ItemsLoadResult = DocumentLoadResult<core::ItemsDocument>;

[[nodiscard]] std::expected<ConfigLoadResult, StoreError> loadConfig(
    const std::filesystem::path& path);
[[nodiscard]] std::expected<ItemsLoadResult, StoreError> loadItems(
    const std::filesystem::path& path);
[[nodiscard]] std::expected<void, StoreError> saveConfig(
    const std::filesystem::path& path,
    const core::ApplicationConfig& config);
[[nodiscard]] std::expected<void, StoreError> saveItems(
    const std::filesystem::path& path,
    const core::ItemsDocument& items);

} // namespace hlaunch::infrastructure::filesystem
