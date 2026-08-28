#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "core/data_model.h"
#include "core/data_validation.h"
#include "infrastructure/json/data_json_codec.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hlaunch::infrastructure::json::JsonIssue;
using hlaunch::infrastructure::json::JsonIssueCode;

template <typename Result>
bool hasIssue(const Result& result, const JsonIssueCode code, const std::string_view path)
{
    return std::ranges::any_of(result.issues, [code, path](const JsonIssue& issue) {
        return issue.code == code && issue.path == path;
    });
}

constexpr std::string_view validConfig = R"({
  "futureRoot": true,
  "appearance": {
    "backdrop": "acrylic",
    "opacityPercent": 95,
    "gridColumns": 6,
    "gridRows": 9
  },
  "activation": {
    "screenEdge": {
      "disableOnFullscreen": true,
      "cooldownMs": 500,
      "pollMs": 40,
      "dwellMs": 300,
      "cornerSizeDip": 16,
      "thicknessDip": 4,
      "edgeMode": "desktopOuter",
      "zones": ["left"],
      "enabled": false,
      "foregroundProcessBlocklist": ["game.exe"],
      "foregroundProcessAllowlist": ["mstsc.exe"],
      "futureEdge": "ignored"
    },
    "hotkey": {
      "behavior": "toggle",
      "key": "Space",
      "modifiers": ["alt"],
      "enabled": true,
      "futureHotkey": 1
    }
  },
  "diagnostics": {
    "loggingEnabled": false
  },
  "schemaVersion": 1
})";

constexpr std::string_view validItems = R"({
  "tabs": [{
    "items": [{
      "lastLaunchedAt": "2026-08-20T09:30:00.125Z",
      "launchCount": 3,
      "runAsAdministrator": false,
      "icon": null,
      "workingDirectory": null,
      "arguments": ["--profile", "work"],
      "target": "C:\\Path\\App.exe",
      "name": "Example",
      "type": "application",
      "id": "fb462c0f-3050-4ddd-a1bc-fbb66008fd9e",
      "futureItem": true
    }],
    "name": "Common",
    "id": "72976493-b090-4ca3-bbb8-d1d97d9f4eaf"
  }],
  "schemaVersion": 1,
  "futureRoot": "ignored"
})";

} // namespace

TEST_CASE("DATA-CONFIG-001 config accepts unordered and unknown fields")
{
    const auto result = hlaunch::infrastructure::json::decodeConfig(validConfig);

    REQUIRE(result.hasValue());
    CHECK(result.issues.empty());
    CHECK(result.value->appearance.backdrop == hlaunch::core::BackdropMode::Acrylic);
    CHECK(result.value->appearance.opacityPercent == 95);
    CHECK(result.value->appearance.gridColumns == 6);
    CHECK(result.value->appearance.gridRows == 9);
    CHECK(result.value->activation.hotkey.enabled);
    CHECK(result.value->activation.hotkey.key == "Space");
    CHECK(result.value->activation.hotkey.modifiers
        == std::vector{hlaunch::core::HotkeyModifier::Alt});
    CHECK_FALSE(result.value->activation.screenEdge.enabled);
    CHECK(result.value->activation.screenEdge.zones
        == std::vector{hlaunch::core::ScreenEdgeZone::Left});
    CHECK(result.value->activation.screenEdge.foregroundProcessBlocklist
        == std::vector<std::string>{"game.exe"});
    CHECK(result.value->activation.screenEdge.foregroundProcessAllowlist
        == std::vector<std::string>{"mstsc.exe"});
    CHECK_FALSE(result.value->diagnostics.loggingEnabled);
}

TEST_CASE("QUALITY-LOG-001 config without diagnostics keeps logging enabled")
{
    std::string legacy{validConfig};
    const auto start = legacy.find("  \"diagnostics\": {");
    REQUIRE(start != std::string::npos);
    const auto end = legacy.find("  \"schemaVersion\":", start);
    REQUIRE(end != std::string::npos);
    legacy.erase(start, end - start);

    const auto result = hlaunch::infrastructure::json::decodeConfig(legacy);
    REQUIRE(result.hasValue());
    CHECK(result.value->diagnostics.loggingEnabled);
}

TEST_CASE("DATA-CONFIG-001 config without appearance uses built-in appearance defaults")
{
    std::string legacy{validConfig};
    const auto start = legacy.find("  \"appearance\": {");
    REQUIRE(start != std::string::npos);
    const auto end = legacy.find("  \"activation\":", start);
    REQUIRE(end != std::string::npos);
    legacy.erase(start, end - start);

    const auto result = hlaunch::infrastructure::json::decodeConfig(legacy);
    REQUIRE(result.hasValue());
    CHECK(result.value->appearance == hlaunch::core::AppearanceConfig{});
}

TEST_CASE("DATA-CONFIG-001 legacy appearance defaults material and opacity")
{
    std::string legacy{validConfig};
    const auto appearance = legacy.find("  \"appearance\": {");
    REQUIRE(appearance != std::string::npos);
    const auto activation = legacy.find("  \"activation\":", appearance);
    REQUIRE(activation != std::string::npos);
    legacy.replace(
        appearance,
        activation - appearance,
        "  \"appearance\": {},\n");
    const auto result = hlaunch::infrastructure::json::decodeConfig(legacy);
    REQUIRE(result.hasValue());
    CHECK(result.value->appearance.backdrop == hlaunch::core::BackdropMode::Acrylic);
    CHECK(result.value->appearance.opacityPercent == 95);
}

TEST_CASE("DATA-CONFIG-001 rejects invalid appearance material and opacity")
{
    std::string invalidBackdrop{validConfig};
    const auto backdrop = invalidBackdrop.find("\"backdrop\": \"acrylic\"");
    REQUIRE(backdrop != std::string::npos);
    invalidBackdrop.replace(backdrop, std::string{"\"backdrop\": \"acrylic\""}.size(),
                            "\"backdrop\": \"glass\"");
    const auto backdropResult = hlaunch::infrastructure::json::decodeConfig(invalidBackdrop);
    CHECK_FALSE(backdropResult.hasValue());
    CHECK(hasIssue(backdropResult, JsonIssueCode::Validation, "$.appearance.backdrop"));

    std::string invalidOpacity{validConfig};
    const auto opacity = invalidOpacity.find("\"opacityPercent\": 95");
    REQUIRE(opacity != std::string::npos);
    invalidOpacity.replace(opacity, std::string{"\"opacityPercent\": 95"}.size(),
                           "\"opacityPercent\": 29");
    const auto opacityResult = hlaunch::infrastructure::json::decodeConfig(invalidOpacity);
    CHECK_FALSE(opacityResult.hasValue());
    CHECK(hasIssue(opacityResult, JsonIssueCode::Validation, "$.appearance.opacityPercent"));
}

TEST_CASE("DATA-CONFIG-001 rejects invalid launcher grid dimensions")
{
    std::string invalidColumns{validConfig};
    const auto columns = invalidColumns.find("\"gridColumns\": 6");
    REQUIRE(columns != std::string::npos);
    invalidColumns.replace(
        columns,
        std::string{"\"gridColumns\": 6"}.size(),
        "\"gridColumns\": 2");
    const auto columnsResult = hlaunch::infrastructure::json::decodeConfig(invalidColumns);
    CHECK_FALSE(columnsResult.hasValue());
    CHECK(hasIssue(columnsResult, JsonIssueCode::Validation, "$.appearance.gridColumns"));

    std::string invalidRows{validConfig};
    const auto rows = invalidRows.find("\"gridRows\": 9");
    REQUIRE(rows != std::string::npos);
    invalidRows.replace(rows, std::string{"\"gridRows\": 9"}.size(), "\"gridRows\": 21");
    const auto rowsResult = hlaunch::infrastructure::json::decodeConfig(invalidRows);
    CHECK_FALSE(rowsResult.hasValue());
    CHECK(hasIssue(rowsResult, JsonIssueCode::Validation, "$.appearance.gridRows"));
}

TEST_CASE("DATA-CONFIG-001 does not treat a malformed appearance as legacy config")
{
    std::string invalid{validConfig};
    const auto start = invalid.find("\"appearance\": {");
    REQUIRE(start != std::string::npos);
    const auto end = invalid.find("  \"activation\":", start);
    REQUIRE(end != std::string::npos);
    invalid.replace(start, end - start, "\"appearance\": 7,\n  ");

    const auto result = hlaunch::infrastructure::json::decodeConfig(invalid);
    CHECK_FALSE(result.hasValue());
    CHECK(hasIssue(result, JsonIssueCode::InvalidJson, "$"));
}

TEST_CASE("DATA-CONFIG-001 config round trips through its persistence DTO")
{
    const auto decoded = hlaunch::infrastructure::json::decodeConfig(validConfig);
    REQUIRE(decoded.hasValue());

    const auto encoded = hlaunch::infrastructure::json::encodeConfig(*decoded.value);
    REQUIRE(encoded.has_value());

    const auto roundTrip = hlaunch::infrastructure::json::decodeConfig(*encoded);
    REQUIRE(roundTrip.hasValue());
    CHECK(roundTrip.issues.empty());
    CHECK(*roundTrip.value == *decoded.value);
}

TEST_CASE("DATA-CONFIG-001 config rejects invalid ranges and missing required fields")
{
    std::string invalidRange{validConfig};
    const auto dwellPosition = invalidRange.find("\"dwellMs\": 300");
    REQUIRE(dwellPosition != std::string::npos);
    invalidRange.replace(dwellPosition, std::string{"\"dwellMs\": 300"}.size(), "\"dwellMs\": 99");

    const auto rangeResult = hlaunch::infrastructure::json::decodeConfig(invalidRange);
    CHECK_FALSE(rangeResult.hasValue());
    CHECK(hasIssue(
        rangeResult,
        JsonIssueCode::Validation,
        "$.activation.screenEdge.dwellMs"));

    constexpr std::string_view missingKey = R"({"schemaVersion":1,"activation":{}})";
    const auto missingResult = hlaunch::infrastructure::json::decodeConfig(missingKey);
    CHECK_FALSE(missingResult.hasValue());
    CHECK(std::ranges::any_of(missingResult.issues, [](const JsonIssue& issue) {
        return issue.code == JsonIssueCode::Validation;
    }));
}

TEST_CASE("ACT-EDGE-001 config validates foreground process lists")
{
    std::string invalidPath{validConfig};
    const auto entry = invalidPath.find("\"game.exe\"");
    REQUIRE(entry != std::string::npos);
    invalidPath.replace(entry, std::string{"\"game.exe\""}.size(), "\"C:\\\\Games\\\\game.exe\"");
    const auto pathResult = hlaunch::infrastructure::json::decodeConfig(invalidPath);
    CHECK_FALSE(pathResult.hasValue());
    CHECK(hasIssue(
        pathResult,
        JsonIssueCode::Validation,
        "$.activation.screenEdge.foregroundProcessBlocklist[0]"));

    std::string invalidExtension{validConfig};
    const auto executable = invalidExtension.find("\"mstsc.exe\"");
    REQUIRE(executable != std::string::npos);
    invalidExtension.replace(
        executable, std::string{"\"mstsc.exe\""}.size(), "\"mstsc.dll\"");
    const auto extensionResult = hlaunch::infrastructure::json::decodeConfig(invalidExtension);
    CHECK_FALSE(extensionResult.hasValue());
    CHECK(hasIssue(
        extensionResult,
        JsonIssueCode::Validation,
        "$.activation.screenEdge.foregroundProcessAllowlist[0]"));
}

TEST_CASE("DATA-CONFIG-001 config rejects malformed JSON and invalid UTF-8")
{
    constexpr std::string_view truncated = R"({"schemaVersion":1,"activation":)";
    const auto truncatedResult = hlaunch::infrastructure::json::decodeConfig(truncated);
    CHECK_FALSE(truncatedResult.hasValue());
    CHECK(hasIssue(truncatedResult, JsonIssueCode::InvalidJson, "$"));

    std::string invalidUtf8{validConfig};
    const auto keyPosition = invalidUtf8.find("Space");
    REQUIRE(keyPosition != std::string::npos);
    invalidUtf8[keyPosition] = static_cast<char>(0xC3);
    invalidUtf8[keyPosition + 1] = '(';

    const auto utf8Result = hlaunch::infrastructure::json::decodeConfig(invalidUtf8);
    CHECK_FALSE(utf8Result.hasValue());
    CHECK(hasIssue(utf8Result, JsonIssueCode::InvalidJson, "$"));
}

TEST_CASE("DATA-CONFIG-001 protects newer schemas from overwrite")
{
    std::string newerSchema{validConfig};
    const auto schemaPosition = newerSchema.find("\"schemaVersion\": 1");
    REQUIRE(schemaPosition != std::string::npos);
    newerSchema.replace(
        schemaPosition,
        std::string{"\"schemaVersion\": 1"}.size(),
        "\"schemaVersion\": 2");

    const auto result = hlaunch::infrastructure::json::decodeConfig(newerSchema);
    CHECK_FALSE(result.hasValue());
    CHECK(result.readOnlyProtection);
    CHECK(hasIssue(result, JsonIssueCode::UnsupportedSchema, "$.schemaVersion"));
}

TEST_CASE("DATA-CONFIG-001 enforces document size and nesting limits")
{
    const std::string oversized(
        hlaunch::infrastructure::json::maxJsonDocumentBytes + 1,
        ' ');
    const auto oversizedResult = hlaunch::infrastructure::json::decodeConfig(oversized);
    CHECK(hasIssue(oversizedResult, JsonIssueCode::InputTooLarge, "$"));

    std::string nested(33, '[');
    nested += '0';
    nested.append(33, ']');
    const auto nestedResult = hlaunch::infrastructure::json::decodeItems(nested);
    CHECK(hasIssue(nestedResult, JsonIssueCode::NestingTooDeep, "$"));
}

TEST_CASE("DATA-CONFIG-001 items decode and round trip")
{
    const auto decoded = hlaunch::infrastructure::json::decodeItems(validItems);
    REQUIRE(decoded.hasValue());
    CHECK(decoded.issues.empty());
    REQUIRE(decoded.value->tabs.size() == 1);
    REQUIRE(decoded.value->tabs[0].items.size() == 1);
    CHECK(decoded.value->tabs[0].items[0].type == hlaunch::core::ItemType::Application);
    CHECK(decoded.value->tabs[0].items[0].gridSlot == 0U);
    CHECK((decoded.value->tabs[0].items[0].arguments == std::vector<std::string>{"--profile", "work"}));

    const auto encoded = hlaunch::infrastructure::json::encodeItems(*decoded.value);
    REQUIRE(encoded.has_value());
    CHECK(encoded->find("\"workingDirectory\":null") != std::string::npos);
    CHECK(encoded->find("\"lastLaunchedAt\":") != std::string::npos);
    CHECK(encoded->find("\"gridSlot\":0") != std::string::npos);

    const auto roundTrip = hlaunch::infrastructure::json::decodeItems(*encoded);
    REQUIRE(roundTrip.hasValue());
    CHECK(roundTrip.issues.empty());
    CHECK(*roundTrip.value == *decoded.value);
}

TEST_CASE("DATA-CONFIG-001 isolates invalid items")
{
    constexpr std::string_view input = R"({
      "schemaVersion": 1,
      "tabs": [{
        "id": "72976493-b090-4ca3-bbb8-d1d97d9f4eaf",
        "name": "Common",
        "items": [
          {
            "id": "fb462c0f-3050-4ddd-a1bc-fbb66008fd9e",
            "type": "application",
            "name": "Valid",
            "target": "C:\\Valid.exe",
            "arguments": [],
            "workingDirectory": null,
            "icon": null,
            "runAsAdministrator": false,
            "launchCount": 0,
            "lastLaunchedAt": null
          },
          {
            "id": "not-a-uuid",
            "type": "application",
            "name": "Bad ID",
            "target": "C:\\Bad.exe",
            "arguments": [],
            "workingDirectory": null,
            "icon": null,
            "runAsAdministrator": false,
            "launchCount": 0,
            "lastLaunchedAt": null
          },
          {
            "id": "b7f4c4ce-cf2f-4a5d-b91a-f284a3b14f62",
            "type": "application",
            "name": "Bad Shape",
            "target": "C:\\BadShape.exe",
            "arguments": "not-an-array",
            "workingDirectory": null,
            "icon": null,
            "runAsAdministrator": false,
            "launchCount": 0,
            "lastLaunchedAt": null
          }
        ]
      }]
    })";

    const auto result = hlaunch::infrastructure::json::decodeItems(input);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->tabs.size() == 1);
    REQUIRE(result.value->tabs[0].items.size() == 1);
    CHECK(result.value->tabs[0].items[0].name == "Valid");
    CHECK(hasIssue(result, JsonIssueCode::Validation, "$.tabs[0].items[1].id"));
    CHECK(hasIssue(result, JsonIssueCode::InvalidJson, "$.tabs[0].items[2]"));
}

TEST_CASE("DATA-CONFIG-001 rejects invalid domain data before encoding")
{
    hlaunch::core::ItemsDocument document{};
    document.tabs.push_back(hlaunch::core::Tab{
        .id = "72976493-b090-4ca3-bbb8-d1d97d9f4eaf",
        .name = "Common",
        .items = {hlaunch::core::LaunchItem{
            .id = "fb462c0f-3050-4ddd-a1bc-fbb66008fd9e",
            .type = hlaunch::core::ItemType::Application,
            .name = "Invalid timestamp",
            .target = "C:\\Example.exe",
            .lastLaunchedAt = "not-a-timestamp",
        }},
    });

    const auto result = hlaunch::infrastructure::json::encodeItems(document);
    REQUIRE_FALSE(result.has_value());
    CHECK(std::ranges::any_of(result.error(), [](const JsonIssue& issue) {
        return issue.path == "$.tabs[0].items[0].lastLaunchedAt";
    }));
}

TEST_CASE("DATA-CONFIG-001 validates identifiers and UTC timestamps")
{
    CHECK(hlaunch::core::isValidUuidV4("fb462c0f-3050-4ddd-a1bc-fbb66008fd9e"));
    CHECK_FALSE(hlaunch::core::isValidUuidV4("FB462C0F-3050-4DDD-A1BC-FBB66008FD9E"));
    CHECK(hlaunch::core::isValidUtcTimestamp("2024-02-29T23:59:59Z"));
    CHECK(hlaunch::core::isValidUtcTimestamp("2026-08-20T09:30:00.125Z"));
    CHECK_FALSE(hlaunch::core::isValidUtcTimestamp("2025-02-29T09:30:00Z"));
    CHECK_FALSE(hlaunch::core::isValidUtcTimestamp("2026-08-20T09:30:00+08:00"));
}
