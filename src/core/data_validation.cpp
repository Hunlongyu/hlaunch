#include "core/data_validation.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>

namespace hlaunch::core {
namespace {

constexpr std::size_t maxTabCount = 128;
constexpr std::size_t maxTotalItemCount = 10'000;
constexpr std::size_t maxItemsPerTab = 10'000;
constexpr std::size_t maxTabNameLength = 128;
constexpr std::size_t maxItemNameLength = 256;
constexpr std::size_t maxPathLength = 32'768;
constexpr std::size_t maxArgumentCount = 256;

void addIssue(
    std::vector<ValidationIssue>& issues,
    std::string path,
    std::string message)
{
    issues.push_back(ValidationIssue{std::move(path), std::move(message)});
}

bool isLowerHex(const char value) noexcept
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

bool allDigits(std::string_view value) noexcept
{
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

unsigned parseUnsigned(std::string_view value) noexcept
{
    unsigned result = 0;
    for (const char character : value) {
        result = (result * 10U) + static_cast<unsigned>(character - '0');
    }
    return result;
}

template <typename Value>
bool containsDuplicate(const std::vector<Value>& values)
{
    std::unordered_set<Value> unique{};
    for (const auto& value : values) {
        if (!unique.insert(value).second) {
            return true;
        }
    }
    return false;
}

} // namespace

bool isValidUuidV4(const std::string_view value) noexcept
{
    if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-'
        || value[23] != '-') {
        return false;
    }

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            continue;
        }
        if (!isLowerHex(value[index])) {
            return false;
        }
    }

    return value[14] == '4'
        && (value[19] == '8' || value[19] == '9' || value[19] == 'a' || value[19] == 'b');
}

bool isValidUtcTimestamp(const std::string_view value) noexcept
{
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T'
        || value[13] != ':' || value[16] != ':' || value.back() != 'Z') {
        return false;
    }

    if (!allDigits(value.substr(0, 4)) || !allDigits(value.substr(5, 2))
        || !allDigits(value.substr(8, 2)) || !allDigits(value.substr(11, 2))
        || !allDigits(value.substr(14, 2)) || !allDigits(value.substr(17, 2))) {
        return false;
    }

    if (value.size() > 20) {
        if (value[19] != '.' || value.size() == 21
            || !allDigits(value.substr(20, value.size() - 21))) {
            return false;
        }
    }

    const auto year = static_cast<int>(parseUnsigned(value.substr(0, 4)));
    const auto month = parseUnsigned(value.substr(5, 2));
    const auto day = parseUnsigned(value.substr(8, 2));
    const auto hour = parseUnsigned(value.substr(11, 2));
    const auto minute = parseUnsigned(value.substr(14, 2));
    const auto second = parseUnsigned(value.substr(17, 2));

    const std::chrono::year_month_day date{
        std::chrono::year{year},
        std::chrono::month{month},
        std::chrono::day{day},
    };
    return date.ok() && hour <= 23U && minute <= 59U && second <= 60U;
}

std::vector<ValidationIssue> validateConfig(const ApplicationConfig& config)
{
    std::vector<ValidationIssue> issues{};
    if (config.schemaVersion != currentSchemaVersion) {
        addIssue(issues, "$.schemaVersion", "schemaVersion must be 1");
    }

    if (config.activation.hotkey.modifiers.empty()) {
        addIssue(issues, "$.activation.hotkey.modifiers", "at least one modifier is required");
    }
    if (containsDuplicate(config.activation.hotkey.modifiers)) {
        addIssue(issues, "$.activation.hotkey.modifiers", "modifiers must be unique");
    }
    if (config.activation.hotkey.key.empty() || config.activation.hotkey.key.size() > 32) {
        addIssue(issues, "$.activation.hotkey.key", "key length must be between 1 and 32");
    }

    const auto& edge = config.activation.screenEdge;
    if (edge.zones.empty()) {
        addIssue(issues, "$.activation.screenEdge.zones", "at least one edge zone is required");
    }
    if (containsDuplicate(edge.zones)) {
        addIssue(issues, "$.activation.screenEdge.zones", "edge zones must be unique");
    }
    if (!std::isfinite(edge.thicknessDip) || edge.thicknessDip < 2.0 || edge.thicknessDip > 16.0) {
        addIssue(issues, "$.activation.screenEdge.thicknessDip", "value must be between 2 and 16 DIP");
    }
    if (!std::isfinite(edge.cornerSizeDip) || edge.cornerSizeDip < 8.0 || edge.cornerSizeDip > 64.0) {
        addIssue(issues, "$.activation.screenEdge.cornerSizeDip", "value must be between 8 and 64 DIP");
    }
    if (edge.dwellMs < 100 || edge.dwellMs > 1'000) {
        addIssue(issues, "$.activation.screenEdge.dwellMs", "value must be between 100 and 1000 ms");
    }
    if (edge.pollMs < 30 || edge.pollMs > 50) {
        addIssue(issues, "$.activation.screenEdge.pollMs", "value must be between 30 and 50 ms");
    }
    if (edge.cooldownMs > 5'000) {
        addIssue(issues, "$.activation.screenEdge.cooldownMs", "value must not exceed 5000 ms");
    }
    return issues;
}

std::vector<ValidationIssue> validateItem(const LaunchItem& item, const std::string_view path)
{
    std::vector<ValidationIssue> issues{};
    const std::string prefix{path};
    if (!isValidUuidV4(item.id)) {
        addIssue(issues, prefix + ".id", "id must be a lowercase UUID v4");
    }
    if (item.name.empty() || item.name.size() > maxItemNameLength) {
        addIssue(issues, prefix + ".name", "name length must be between 1 and 256");
    }
    if (item.target.empty() || item.target.size() > maxPathLength) {
        addIssue(issues, prefix + ".target", "target length must be between 1 and 32768");
    }
    if (item.arguments.size() > maxArgumentCount) {
        addIssue(issues, prefix + ".arguments", "argument count must not exceed 256");
    }
    for (std::size_t index = 0; index < item.arguments.size(); ++index) {
        if (item.arguments[index].size() > maxPathLength) {
            addIssue(
                issues,
                prefix + ".arguments[" + std::to_string(index) + "]",
                "argument length must not exceed 32768");
        }
    }
    if (item.workingDirectory && item.workingDirectory->size() > maxPathLength) {
        addIssue(issues, prefix + ".workingDirectory", "value length must not exceed 32768");
    }
    if (item.icon && item.icon->size() > maxPathLength) {
        addIssue(issues, prefix + ".icon", "value length must not exceed 32768");
    }
    if (item.lastLaunchedAt && !isValidUtcTimestamp(*item.lastLaunchedAt)) {
        addIssue(issues, prefix + ".lastLaunchedAt", "timestamp must be UTC RFC 3339");
    }
    return issues;
}

std::vector<ValidationIssue> validateTab(const Tab& tab, const std::string_view path)
{
    std::vector<ValidationIssue> issues{};
    const std::string prefix{path};
    if (!isValidUuidV4(tab.id)) {
        addIssue(issues, prefix + ".id", "id must be a lowercase UUID v4");
    }
    if (tab.name.empty() || tab.name.size() > maxTabNameLength) {
        addIssue(issues, prefix + ".name", "name length must be between 1 and 128");
    }
    if (tab.items.size() > maxItemsPerTab) {
        addIssue(issues, prefix + ".items", "item count must not exceed 10000");
    }
    for (std::size_t index = 0; index < tab.items.size(); ++index) {
        auto itemIssues = validateItem(tab.items[index], prefix + ".items[" + std::to_string(index) + "]");
        issues.insert(issues.end(), itemIssues.begin(), itemIssues.end());
    }
    return issues;
}

std::vector<ValidationIssue> validateItemsDocument(const ItemsDocument& document)
{
    std::vector<ValidationIssue> issues{};
    if (document.schemaVersion != currentSchemaVersion) {
        addIssue(issues, "$.schemaVersion", "schemaVersion must be 1");
    }
    if (document.tabs.size() > maxTabCount) {
        addIssue(issues, "$.tabs", "tab count must not exceed 128");
    }

    std::size_t totalItems = 0;
    std::unordered_set<std::string> tabIds{};
    std::unordered_set<std::string> itemIds{};
    for (std::size_t tabIndex = 0; tabIndex < document.tabs.size(); ++tabIndex) {
        const auto& tab = document.tabs[tabIndex];
        const auto tabPath = "$.tabs[" + std::to_string(tabIndex) + "]";
        if (!tabIds.insert(tab.id).second) {
            addIssue(issues, tabPath + ".id", "tab id must be unique");
        }
        auto tabIssues = validateTab(tab, tabPath);
        issues.insert(issues.end(), tabIssues.begin(), tabIssues.end());

        totalItems += tab.items.size();
        for (std::size_t itemIndex = 0; itemIndex < tab.items.size(); ++itemIndex) {
            if (!itemIds.insert(tab.items[itemIndex].id).second) {
                addIssue(
                    issues,
                    tabPath + ".items[" + std::to_string(itemIndex) + "].id",
                    "item id must be unique");
            }
        }
    }
    if (totalItems > maxTotalItemCount) {
        addIssue(issues, "$.tabs", "total item count must not exceed 10000");
    }
    return issues;
}

} // namespace hlaunch::core
