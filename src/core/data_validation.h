#pragma once

#include "core/data_model.h"

#include <string>
#include <string_view>
#include <vector>

namespace hlaunch::core {

struct ValidationIssue {
    std::string path{};
    std::string message{};

    bool operator==(const ValidationIssue&) const = default;
};

[[nodiscard]] bool isValidUuidV4(std::string_view value) noexcept;
[[nodiscard]] bool isValidUtcTimestamp(std::string_view value) noexcept;

[[nodiscard]] std::vector<ValidationIssue> validateConfig(const ApplicationConfig& config);
[[nodiscard]] std::vector<ValidationIssue> validateItem(
    const LaunchItem& item,
    std::string_view path = "$.item");
[[nodiscard]] std::vector<ValidationIssue> validateTab(
    const Tab& tab,
    std::string_view path = "$.tab");
[[nodiscard]] std::vector<ValidationIssue> validateItemsDocument(const ItemsDocument& document);

} // namespace hlaunch::core
