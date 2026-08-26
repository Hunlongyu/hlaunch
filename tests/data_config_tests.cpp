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
    "theme": "dark"
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
    CHECK(result.value->appearance.theme == hlaunch::core::ThemeMode::Dark);
    CHECK(result.value->activation.hotkey.enabled);
    CHECK(result.value->activation.hotkey.key == "Space");
    CHECK(result.value->activation.hotkey.modifiers
        == std::vector{hlaunch::core::HotkeyModifier::Alt});
    CHECK_FALSE(result.value->activation.screenEdge.enabled);
    CHECK(result.value->activation.screenEdge.zones
        == std::vector{hlaunch::core::ScreenEdgeZone::Left});
}

TEST_CASE("DATA-CONFIG-001 legacy config without appearance keeps the dark theme")
{
    std::string legacy{validConfig};
    const auto start = legacy.find("  \"appearance\": {");
    REQUIRE(start != std::string::npos);
    const auto end = legacy.find("  \"activation\":", start);
    REQUIRE(end != std::string::npos);
    legacy.erase(start, end - start);

    const auto result = hlaunch::infrastructure::json::decodeConfig(legacy);
    REQUIRE(result.hasValue());
    CHECK(result.issues.empty());
    CHECK(result.value->appearance.theme == hlaunch::core::ThemeMode::Dark);
}

TEST_CASE("DATA-CONFIG-001 rejects an unknown appearance theme")
{
    std::string invalid{validConfig};
    const auto position = invalid.find("\"theme\": \"dark\"");
    REQUIRE(position != std::string::npos);
    invalid.replace(position, std::string{"\"theme\": \"dark\""}.size(), "\"theme\": \"neon\"");

    const auto result = hlaunch::infrastructure::json::decodeConfig(invalid);
    CHECK_FALSE(result.hasValue());
    CHECK(hasIssue(result, JsonIssueCode::Validation, "$.appearance.theme"));
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
    CHECK(hasIssue(missingResult, JsonIssueCode::InvalidJson, "$"));
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
    CHECK((decoded.value->tabs[0].items[0].arguments == std::vector<std::string>{"--profile", "work"}));

    const auto encoded = hlaunch::infrastructure::json::encodeItems(*decoded.value);
    REQUIRE(encoded.has_value());
    CHECK(encoded->find("\"workingDirectory\":null") != std::string::npos);
    CHECK(encoded->find("\"lastLaunchedAt\":") != std::string::npos);

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
