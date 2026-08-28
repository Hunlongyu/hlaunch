#include "infrastructure/json/data_json_codec.h"

#include "core/data_validation.h"
#include "core/item_operations.h"

#include <glaze/glaze.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hlaunch::infrastructure::json {
namespace detail {

constexpr std::size_t maxTabCount = 128;
constexpr std::size_t maxTotalItemCount = 10'000;

struct HotkeyDto {
    bool enabled{};
    std::vector<std::string> modifiers{};
    std::string key{};
    std::string behavior{};
};

struct ScreenEdgeDto {
    bool enabled{};
    std::vector<std::string> zones{};
    std::string edgeMode{};
    double thicknessDip{};
    double cornerSizeDip{};
    std::int64_t dwellMs{};
    std::int64_t pollMs{};
    std::int64_t cooldownMs{};
    bool disableOnFullscreen{};
    std::vector<std::string> foregroundProcessBlocklist{};
    std::vector<std::string> foregroundProcessAllowlist{};
};

struct ActivationDto {
    HotkeyDto hotkey{};
    ScreenEdgeDto screenEdge{};
};

struct AppearanceDto {
    std::optional<std::string> backdrop{};
    std::optional<std::int64_t> opacityPercent{};
    std::optional<std::int64_t> gridColumns{};
    std::optional<std::int64_t> gridRows{};
};

struct DiagnosticsDto {
    std::optional<bool> loggingEnabled{};
};

struct ConfigDto {
    std::int64_t schemaVersion{};
    AppearanceDto appearance{};
    ActivationDto activation{};
    DiagnosticsDto diagnostics{};
};

struct ItemDto {
    std::string id{};
    std::string type{};
    std::string name{};
    std::string target{};
    std::vector<std::string> arguments{};
    std::optional<std::string> workingDirectory{};
    std::optional<std::string> icon{};
    bool runAsAdministrator{};
    std::uint64_t launchCount{};
    std::optional<std::string> lastLaunchedAt{};
    std::optional<std::int64_t> gridSlot{};
};

struct ItemWithoutGridSlotDto {
    std::string id{};
    std::string type{};
    std::string name{};
    std::string target{};
    std::vector<std::string> arguments{};
    std::optional<std::string> workingDirectory{};
    std::optional<std::string> icon{};
    bool runAsAdministrator{};
    std::uint64_t launchCount{};
    std::optional<std::string> lastLaunchedAt{};
};

struct TabReadDto {
    std::string id{};
    std::string name{};
    std::vector<glz::raw_json> items{};
};

struct ItemsReadDto {
    std::int64_t schemaVersion{};
    std::vector<TabReadDto> tabs{};
};

struct TabWriteDto {
    std::string id{};
    std::string name{};
    std::vector<ItemDto> items{};
};

struct ItemsWriteDto {
    std::uint32_t schemaVersion{};
    std::vector<TabWriteDto> tabs{};
};

constexpr glz::opts makeReadOptions()
{
    auto options = glz::opts{};
    options.error_on_unknown_keys = false;
    options.null_terminated = false;
    options.skip_null_members = false;
    options.error_on_missing_keys = false;
    return options;
}

constexpr glz::opts makeWriteOptions()
{
    auto options = glz::opts{};
    options.skip_null_members = false;
    return options;
}

inline constexpr auto readOptions = makeReadOptions();
inline constexpr auto writeOptions = makeWriteOptions();

JsonIssue makeIssue(
    const JsonIssueCode code,
    std::string path,
    std::string message)
{
    return JsonIssue{code, std::move(path), std::move(message)};
}

std::vector<JsonIssue> convertIssues(const std::vector<core::ValidationIssue>& issues)
{
    std::vector<JsonIssue> converted{};
    converted.reserve(issues.size());
    for (const auto& issue : issues) {
        converted.push_back(makeIssue(JsonIssueCode::Validation, issue.path, issue.message));
    }
    return converted;
}

void appendIssues(std::vector<JsonIssue>& destination, std::vector<JsonIssue> source)
{
    destination.insert(
        destination.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end()));
}

std::optional<JsonIssue> preflight(std::string_view input)
{
    if (input.size() > maxJsonDocumentBytes) {
        return makeIssue(JsonIssueCode::InputTooLarge, "$", "JSON document exceeds 8 MiB");
    }
    if (!glz::validate_utf8(input.data(), input.size())) {
        return makeIssue(JsonIssueCode::InvalidJson, "$", "JSON document is not valid UTF-8");
    }

    std::size_t depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char character : input) {
        if (inString) {
            if (escaped) {
                escaped = false;
            }
            else if (character == '\\') {
                escaped = true;
            }
            else if (character == '"') {
                inString = false;
            }
            continue;
        }

        if (character == '"') {
            inString = true;
        }
        else if (character == '{' || character == '[') {
            ++depth;
            if (depth > maxJsonNestingDepth) {
                return makeIssue(
                    JsonIssueCode::NestingTooDeep,
                    "$",
                    "JSON nesting exceeds 32 levels");
            }
        }
        else if ((character == '}' || character == ']') && depth > 0) {
            --depth;
        }
    }
    return std::nullopt;
}

template <typename Dto>
std::optional<JsonIssue> parseDto(Dto& dto, const std::string_view input, std::string path)
{
    const auto error = glz::read<readOptions>(dto, input);
    if (!error) {
        return std::nullopt;
    }
    return makeIssue(
        JsonIssueCode::InvalidJson,
        std::move(path),
        glz::format_error(error, input));
}

template <typename Result>
bool checkSchema(const std::int64_t schemaVersion, Result& result)
{
    if (schemaVersion > static_cast<std::int64_t>(core::currentSchemaVersion)) {
        result.readOnlyProtection = true;
        result.issues.push_back(makeIssue(
            JsonIssueCode::UnsupportedSchema,
            "$.schemaVersion",
            "schemaVersion is newer than this HLaunch build"));
        return false;
    }
    if (schemaVersion != static_cast<std::int64_t>(core::currentSchemaVersion)) {
        result.issues.push_back(makeIssue(
            JsonIssueCode::Validation,
            "$.schemaVersion",
            "schemaVersion must be 1"));
        return false;
    }
    return true;
}

std::optional<core::HotkeyModifier> hotkeyModifierFromString(const std::string_view value)
{
    if (value == "alt") {
        return core::HotkeyModifier::Alt;
    }
    if (value == "control") {
        return core::HotkeyModifier::Control;
    }
    if (value == "shift") {
        return core::HotkeyModifier::Shift;
    }
    if (value == "win") {
        return core::HotkeyModifier::Win;
    }
    return std::nullopt;
}

std::string_view hotkeyModifierToString(const core::HotkeyModifier value)
{
    switch (value) {
    case core::HotkeyModifier::Alt:
        return "alt";
    case core::HotkeyModifier::Control:
        return "control";
    case core::HotkeyModifier::Shift:
        return "shift";
    case core::HotkeyModifier::Win:
        return "win";
    }
    return {};
}

std::optional<core::ScreenEdgeZone> edgeZoneFromString(const std::string_view value)
{
    if (value == "left") {
        return core::ScreenEdgeZone::Left;
    }
    if (value == "right") {
        return core::ScreenEdgeZone::Right;
    }
    if (value == "top") {
        return core::ScreenEdgeZone::Top;
    }
    if (value == "bottom") {
        return core::ScreenEdgeZone::Bottom;
    }
    if (value == "topLeft") {
        return core::ScreenEdgeZone::TopLeft;
    }
    if (value == "topRight") {
        return core::ScreenEdgeZone::TopRight;
    }
    if (value == "bottomLeft") {
        return core::ScreenEdgeZone::BottomLeft;
    }
    if (value == "bottomRight") {
        return core::ScreenEdgeZone::BottomRight;
    }
    return std::nullopt;
}

std::string_view edgeZoneToString(const core::ScreenEdgeZone value)
{
    switch (value) {
    case core::ScreenEdgeZone::Left:
        return "left";
    case core::ScreenEdgeZone::Right:
        return "right";
    case core::ScreenEdgeZone::Top:
        return "top";
    case core::ScreenEdgeZone::Bottom:
        return "bottom";
    case core::ScreenEdgeZone::TopLeft:
        return "topLeft";
    case core::ScreenEdgeZone::TopRight:
        return "topRight";
    case core::ScreenEdgeZone::BottomLeft:
        return "bottomLeft";
    case core::ScreenEdgeZone::BottomRight:
        return "bottomRight";
    }
    return {};
}

std::optional<core::ItemType> itemTypeFromString(const std::string_view value)
{
    if (value == "application") {
        return core::ItemType::Application;
    }
    if (value == "file") {
        return core::ItemType::File;
    }
    if (value == "folder") {
        return core::ItemType::Folder;
    }
    if (value == "url") {
        return core::ItemType::Url;
    }
    if (value == "shortcut") {
        return core::ItemType::Shortcut;
    }
    return std::nullopt;
}

std::string_view itemTypeToString(const core::ItemType value)
{
    switch (value) {
    case core::ItemType::Application:
        return "application";
    case core::ItemType::File:
        return "file";
    case core::ItemType::Folder:
        return "folder";
    case core::ItemType::Url:
        return "url";
    case core::ItemType::Shortcut:
        return "shortcut";
    }
    return {};
}

std::optional<std::uint32_t> checkedMilliseconds(
    const std::int64_t value,
    std::string path,
    std::vector<JsonIssue>& issues)
{
    if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        issues.push_back(makeIssue(
            JsonIssueCode::Validation,
            std::move(path),
            "millisecond value is outside uint32 range"));
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

std::optional<core::ApplicationConfig> configFromDto(
    const ConfigDto& dto,
    std::vector<JsonIssue>& issues)
{
    core::ApplicationConfig config{};
    config.schemaVersion = static_cast<std::uint32_t>(dto.schemaVersion);
    const auto backdrop = dto.appearance.backdrop.value_or("acrylic");
    if (backdrop == "solid") {
        config.appearance.backdrop = core::BackdropMode::Solid;
    }
    else if (backdrop == "mica") {
        config.appearance.backdrop = core::BackdropMode::Mica;
    }
    else if (backdrop == "acrylic") {
        config.appearance.backdrop = core::BackdropMode::Acrylic;
    }
    else if (backdrop == "tabbed") {
        config.appearance.backdrop = core::BackdropMode::Tabbed;
    }
    else {
        issues.push_back(makeIssue(
            JsonIssueCode::Validation,
            "$.appearance.backdrop",
            "backdrop must be solid, mica, acrylic, or tabbed"));
    }
    const auto opacity = dto.appearance.opacityPercent.value_or(95);
    if (opacity >= 30 && opacity <= 100) {
        config.appearance.opacityPercent = static_cast<std::uint8_t>(opacity);
    }
    else {
        issues.push_back(makeIssue(
            JsonIssueCode::Validation,
            "$.appearance.opacityPercent",
            "opacityPercent must be between 30 and 100"));
    }
    const auto gridColumns = dto.appearance.gridColumns.value_or(
        core::defaultLauncherGridColumns);
    if (gridColumns >= core::minimumLauncherGridColumns
        && gridColumns <= core::maximumLauncherGridColumns) {
        config.appearance.gridColumns = static_cast<std::uint16_t>(gridColumns);
    }
    else {
        issues.push_back(makeIssue(
            JsonIssueCode::Validation,
            "$.appearance.gridColumns",
            "gridColumns must be between 3 and 20"));
    }
    const auto gridRows = dto.appearance.gridRows.value_or(
        core::defaultLauncherGridRows);
    if (gridRows >= core::minimumLauncherGridRows
        && gridRows <= core::maximumLauncherGridRows) {
        config.appearance.gridRows = static_cast<std::uint16_t>(gridRows);
    }
    else {
        issues.push_back(makeIssue(
            JsonIssueCode::Validation,
            "$.appearance.gridRows",
            "gridRows must be between 3 and 20"));
    }
    config.activation.hotkey.enabled = dto.activation.hotkey.enabled;
    config.activation.hotkey.key = dto.activation.hotkey.key;

    config.activation.hotkey.modifiers.clear();
    for (std::size_t index = 0; index < dto.activation.hotkey.modifiers.size(); ++index) {
        const auto modifier = hotkeyModifierFromString(dto.activation.hotkey.modifiers[index]);
        if (!modifier) {
            issues.push_back(makeIssue(
                JsonIssueCode::Validation,
                "$.activation.hotkey.modifiers[" + std::to_string(index) + "]",
                "unknown hotkey modifier"));
            continue;
        }
        config.activation.hotkey.modifiers.push_back(*modifier);
    }

    if (dto.activation.hotkey.behavior != "toggle") {
        issues.push_back(makeIssue(
            JsonIssueCode::Validation,
            "$.activation.hotkey.behavior",
            "behavior must be toggle"));
    }

    auto& edge = config.activation.screenEdge;
    edge.enabled = dto.activation.screenEdge.enabled;
    edge.zones.clear();
    for (std::size_t index = 0; index < dto.activation.screenEdge.zones.size(); ++index) {
        const auto zone = edgeZoneFromString(dto.activation.screenEdge.zones[index]);
        if (!zone) {
            issues.push_back(makeIssue(
                JsonIssueCode::Validation,
                "$.activation.screenEdge.zones[" + std::to_string(index) + "]",
                "unknown screen edge zone"));
            continue;
        }
        edge.zones.push_back(*zone);
    }

    if (dto.activation.screenEdge.edgeMode == "desktopOuter") {
        edge.edgeMode = core::ScreenEdgeMode::DesktopOuter;
    }
    else if (dto.activation.screenEdge.edgeMode == "everyMonitor") {
        edge.edgeMode = core::ScreenEdgeMode::EveryMonitor;
    }
    else {
        issues.push_back(makeIssue(
            JsonIssueCode::Validation,
            "$.activation.screenEdge.edgeMode",
            "unknown screen edge mode"));
    }

    edge.thicknessDip = dto.activation.screenEdge.thicknessDip;
    edge.cornerSizeDip = dto.activation.screenEdge.cornerSizeDip;
    edge.disableOnFullscreen = dto.activation.screenEdge.disableOnFullscreen;
    edge.foregroundProcessBlocklist = dto.activation.screenEdge.foregroundProcessBlocklist;
    edge.foregroundProcessAllowlist = dto.activation.screenEdge.foregroundProcessAllowlist;

    const auto dwell = checkedMilliseconds(
        dto.activation.screenEdge.dwellMs,
        "$.activation.screenEdge.dwellMs",
        issues);
    const auto poll = checkedMilliseconds(
        dto.activation.screenEdge.pollMs,
        "$.activation.screenEdge.pollMs",
        issues);
    const auto cooldown = checkedMilliseconds(
        dto.activation.screenEdge.cooldownMs,
        "$.activation.screenEdge.cooldownMs",
        issues);
    if (dwell) {
        edge.dwellMs = *dwell;
    }
    if (poll) {
        edge.pollMs = *poll;
    }
    if (cooldown) {
        edge.cooldownMs = *cooldown;
    }
    config.diagnostics.loggingEnabled = dto.diagnostics.loggingEnabled.value_or(true);

    appendIssues(issues, convertIssues(core::validateConfig(config)));
    if (!issues.empty()) {
        return std::nullopt;
    }
    return config;
}

ConfigDto configToDto(const core::ApplicationConfig& config)
{
    ConfigDto dto{};
    dto.schemaVersion = config.schemaVersion;
    dto.appearance = AppearanceDto{
        .backdrop = [&config] {
            switch (config.appearance.backdrop) {
            case core::BackdropMode::Solid: return "solid";
            case core::BackdropMode::Mica: return "mica";
            case core::BackdropMode::Acrylic: return "acrylic";
            case core::BackdropMode::Tabbed: return "tabbed";
            }
            return "acrylic";
        }(),
        .opacityPercent = config.appearance.opacityPercent,
        .gridColumns = config.appearance.gridColumns,
        .gridRows = config.appearance.gridRows,
    };
    dto.activation.hotkey.enabled = config.activation.hotkey.enabled;
    for (const auto modifier : config.activation.hotkey.modifiers) {
        dto.activation.hotkey.modifiers.emplace_back(hotkeyModifierToString(modifier));
    }
    dto.activation.hotkey.key = config.activation.hotkey.key;
    dto.activation.hotkey.behavior = "toggle";

    const auto& edge = config.activation.screenEdge;
    dto.activation.screenEdge.enabled = edge.enabled;
    for (const auto zone : edge.zones) {
        dto.activation.screenEdge.zones.emplace_back(edgeZoneToString(zone));
    }
    dto.activation.screenEdge.edgeMode = edge.edgeMode == core::ScreenEdgeMode::DesktopOuter
        ? "desktopOuter"
        : "everyMonitor";
    dto.activation.screenEdge.thicknessDip = edge.thicknessDip;
    dto.activation.screenEdge.cornerSizeDip = edge.cornerSizeDip;
    dto.activation.screenEdge.dwellMs = edge.dwellMs;
    dto.activation.screenEdge.pollMs = edge.pollMs;
    dto.activation.screenEdge.cooldownMs = edge.cooldownMs;
    dto.activation.screenEdge.disableOnFullscreen = edge.disableOnFullscreen;
    dto.activation.screenEdge.foregroundProcessBlocklist = edge.foregroundProcessBlocklist;
    dto.activation.screenEdge.foregroundProcessAllowlist = edge.foregroundProcessAllowlist;
    dto.diagnostics.loggingEnabled = config.diagnostics.loggingEnabled;
    return dto;
}

std::optional<core::LaunchItem> itemFromDto(
    const ItemDto& dto,
    const std::string& path,
    std::vector<JsonIssue>& issues)
{
    const auto type = itemTypeFromString(dto.type);
    if (!type) {
        issues.push_back(makeIssue(JsonIssueCode::Validation, path + ".type", "unknown item type"));
        return std::nullopt;
    }

    std::optional<std::uint32_t> gridSlot{};
    if (dto.gridSlot) {
        if (*dto.gridSlot < 0
            || *dto.gridSlot >= static_cast<std::int64_t>(core::maximumGridSlotsPerTab)) {
            issues.push_back(makeIssue(
                JsonIssueCode::Validation,
                path + ".gridSlot",
                "gridSlot must be between 0 and 9999"));
            return std::nullopt;
        }
        gridSlot = static_cast<std::uint32_t>(*dto.gridSlot);
    }

    core::LaunchItem item{
        .id = dto.id,
        .type = *type,
        .name = dto.name,
        .target = dto.target,
        .arguments = dto.arguments,
        .workingDirectory = dto.workingDirectory,
        .icon = dto.icon,
        .runAsAdministrator = dto.runAsAdministrator,
        .launchCount = dto.launchCount,
        .lastLaunchedAt = dto.lastLaunchedAt,
        .gridSlot = gridSlot,
    };
    auto itemIssues = convertIssues(core::validateItem(item, path));
    if (!itemIssues.empty()) {
        appendIssues(issues, std::move(itemIssues));
        return std::nullopt;
    }
    return item;
}

ItemDto itemToDto(const core::LaunchItem& item)
{
    return ItemDto{
        .id = item.id,
        .type = std::string{itemTypeToString(item.type)},
        .name = item.name,
        .target = item.target,
        .arguments = item.arguments,
        .workingDirectory = item.workingDirectory,
        .icon = item.icon,
        .runAsAdministrator = item.runAsAdministrator,
        .launchCount = item.launchCount,
        .lastLaunchedAt = item.lastLaunchedAt,
        .gridSlot = item.gridSlot
            ? std::optional<std::int64_t>{static_cast<std::int64_t>(*item.gridSlot)}
            : std::nullopt,
    };
}

ItemsWriteDto itemsToDto(const core::ItemsDocument& document)
{
    ItemsWriteDto dto{.schemaVersion = document.schemaVersion};
    dto.tabs.reserve(document.tabs.size());
    for (const auto& tab : document.tabs) {
        TabWriteDto tabDto{.id = tab.id, .name = tab.name};
        tabDto.items.reserve(tab.items.size());
        for (const auto& item : tab.items) {
            tabDto.items.push_back(itemToDto(item));
        }
        dto.tabs.push_back(std::move(tabDto));
    }
    return dto;
}

template <typename Dto>
JsonEncodeResult encodeDto(const Dto& dto)
{
    std::string output{};
    const auto error = glz::write<writeOptions>(dto, output);
    if (error) {
        return std::unexpected(std::vector<JsonIssue>{makeIssue(
            JsonIssueCode::InvalidJson,
            "$",
            glz::format_error(error, output))});
    }
    if (output.size() > maxJsonDocumentBytes) {
        return std::unexpected(std::vector<JsonIssue>{makeIssue(
            JsonIssueCode::InputTooLarge,
            "$",
            "encoded JSON document exceeds 8 MiB")});
    }
    return output;
}

} // namespace detail

using namespace detail;

ConfigDecodeResult decodeConfig(const std::string_view input)
{
    ConfigDecodeResult result{};
    if (const auto issue = preflight(input)) {
        result.issues.push_back(*issue);
        return result;
    }

    ConfigDto dto{};
    if (const auto issue = parseDto(dto, input, "$")) {
        result.issues.push_back(*issue);
        return result;
    }
    if (!checkSchema(dto.schemaVersion, result)) {
        return result;
    }

    result.value = configFromDto(dto, result.issues);
    return result;
}

ItemsDecodeResult decodeItems(const std::string_view input)
{
    ItemsDecodeResult result{};
    if (const auto issue = preflight(input)) {
        result.issues.push_back(*issue);
        return result;
    }

    ItemsReadDto dto{};
    if (const auto issue = parseDto(dto, input, "$")) {
        result.issues.push_back(*issue);
        return result;
    }
    if (!checkSchema(dto.schemaVersion, result)) {
        return result;
    }

    core::ItemsDocument document{};
    std::unordered_set<std::string> tabIds{};
    std::unordered_set<std::string> itemIds{};
    std::size_t acceptedItemCount = 0;
    const auto tabCount = std::min(dto.tabs.size(), maxTabCount);
    if (dto.tabs.size() > maxTabCount) {
        result.issues.push_back(makeIssue(
            JsonIssueCode::Validation,
            "$.tabs",
            "tab count exceeds 128; extra tabs were ignored"));
    }

    document.tabs.reserve(tabCount);
    for (std::size_t tabIndex = 0; tabIndex < tabCount; ++tabIndex) {
        const auto& tabDto = dto.tabs[tabIndex];
        const auto tabPath = "$.tabs[" + std::to_string(tabIndex) + "]";
        core::Tab tab{.id = tabDto.id, .name = tabDto.name};

        auto tabIssues = convertIssues(core::validateTab(tab, tabPath));
        if (!tabIds.insert(tab.id).second) {
            tabIssues.push_back(makeIssue(
                JsonIssueCode::Validation,
                tabPath + ".id",
                "duplicate tab id; tab was ignored"));
        }
        if (!tabIssues.empty()) {
            appendIssues(result.issues, std::move(tabIssues));
            continue;
        }

        tab.items.reserve(tabDto.items.size());
        std::unordered_set<std::uint32_t> gridSlots{};
        for (std::size_t itemIndex = 0; itemIndex < tabDto.items.size(); ++itemIndex) {
            const auto itemPath = tabPath + ".items[" + std::to_string(itemIndex) + "]";
            if (acceptedItemCount >= maxTotalItemCount) {
                result.issues.push_back(makeIssue(
                    JsonIssueCode::Validation,
                    itemPath,
                    "total item count exceeds 10000; remaining items were ignored"));
                break;
            }

            ItemDto itemDto{};
            if (const auto issue = parseDto(itemDto, tabDto.items[itemIndex].str, itemPath)) {
                ItemWithoutGridSlotDto legacyItem{};
                if (parseDto(legacyItem, tabDto.items[itemIndex].str, itemPath)) {
                    result.issues.push_back(*issue);
                    continue;
                }
                itemDto = ItemDto{
                    .id = std::move(legacyItem.id),
                    .type = std::move(legacyItem.type),
                    .name = std::move(legacyItem.name),
                    .target = std::move(legacyItem.target),
                    .arguments = std::move(legacyItem.arguments),
                    .workingDirectory = std::move(legacyItem.workingDirectory),
                    .icon = std::move(legacyItem.icon),
                    .runAsAdministrator = legacyItem.runAsAdministrator,
                    .launchCount = legacyItem.launchCount,
                    .lastLaunchedAt = std::move(legacyItem.lastLaunchedAt),
                };
            }
            auto item = itemFromDto(itemDto, itemPath, result.issues);
            if (!item) {
                continue;
            }
            if (!itemIds.insert(item->id).second) {
                result.issues.push_back(makeIssue(
                    JsonIssueCode::Validation,
                    itemPath + ".id",
                    "duplicate item id; item was ignored"));
                continue;
            }
            if (item->gridSlot && !gridSlots.insert(*item->gridSlot).second) {
                result.issues.push_back(makeIssue(
                    JsonIssueCode::Validation,
                    itemPath + ".gridSlot",
                    "duplicate gridSlot was reassigned"));
                item->gridSlot.reset();
            }
            tab.items.push_back(std::move(*item));
            ++acceptedItemCount;
        }
        core::normalizeGridSlots(tab);
        document.tabs.push_back(std::move(tab));
    }

    result.value = std::move(document);
    return result;
}

JsonEncodeResult encodeConfig(const core::ApplicationConfig& config)
{
    auto issues = convertIssues(core::validateConfig(config));
    if (!issues.empty()) {
        return std::unexpected(std::move(issues));
    }
    return encodeDto(configToDto(config));
}

JsonEncodeResult encodeItems(const core::ItemsDocument& document)
{
    auto normalized = document;
    core::normalizeGridSlots(normalized);
    auto issues = convertIssues(core::validateItemsDocument(normalized));
    if (!issues.empty()) {
        return std::unexpected(std::move(issues));
    }
    return encodeDto(itemsToDto(normalized));
}

} // namespace hlaunch::infrastructure::json
