#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/search_index.h"
#include "platform/windows/search_text.h"

#include <string>

namespace {

hlaunch::core::LaunchItem item(
    std::string id,
    std::string name,
    const std::uint64_t launchCount = 0,
    std::optional<std::string> lastLaunchedAt = std::nullopt)
{
    return hlaunch::core::LaunchItem{
        .id = std::move(id),
        .name = std::move(name),
        .launchCount = launchCount,
        .lastLaunchedAt = std::move(lastLaunchedAt),
    };
}

std::optional<std::wstring> normalizeItemName(const std::string_view value)
{
    return hlaunch::platform::windows::normalizeSearchText(value);
}

} // namespace

TEST_CASE("PROD-GRID-001 search ranks exact, prefix, substring and fuzzy matches")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {
            hlaunch::core::Tab{
                .id = "11111111-1111-4111-8111-111111111111",
                .name = "Work",
                .items = {
                    item("11111111-1111-4111-8111-111111111101", "My Alpha Tool"),
                    item("11111111-1111-4111-8111-111111111102", "Alphabet"),
                },
            },
            hlaunch::core::Tab{
                .id = "22222222-2222-4222-8222-222222222222",
                .name = "Tools",
                .items = {
                    item("22222222-2222-4222-8222-222222222201", "A-long-plot-helper-app"),
                    item("22222222-2222-4222-8222-222222222202", "Alpha"),
                },
            },
        },
    };
    hlaunch::core::SearchIndex index{};
    index.rebuild(document, normalizeItemName);
    const auto query = hlaunch::platform::windows::normalizeSearchText(L"ALPHA");
    REQUIRE(query.has_value());

    const auto results = index.search(*query);
    REQUIRE(results.size() == 4);
    CHECK(results[0].matchKind == hlaunch::core::SearchMatchKind::Exact);
    CHECK(results[0].tabIndex == 1);
    CHECK(results[1].matchKind == hlaunch::core::SearchMatchKind::Prefix);
    CHECK(results[2].matchKind == hlaunch::core::SearchMatchKind::Substring);
    CHECK(results[3].matchKind == hlaunch::core::SearchMatchKind::Fuzzy);
}

TEST_CASE("PROD-GRID-001 search uses activity and stable ID as deterministic ties")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "All",
            .items = {
                item("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "Same", 3, "2026-08-20T00:00:00Z"),
                item("cccccccc-cccc-4ccc-8ccc-cccccccccccc", "Same", 9, "2026-08-19T00:00:00Z"),
                item("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "Same", 3, "2026-08-20T00:00:00Z"),
            },
        }},
    };
    hlaunch::core::SearchIndex index{};
    index.rebuild(document, normalizeItemName);
    const auto query = hlaunch::platform::windows::normalizeSearchText(L"same");
    REQUIRE(query.has_value());

    const auto results = index.search(*query);
    REQUIRE(results.size() == 3);
    CHECK(results[0].itemIndex == 1);
    CHECK(results[1].itemIndex == 2);
    CHECK(results[2].itemIndex == 0);
}

TEST_CASE("PROD-GRID-001 search normalization is Unicode case insensitive")
{
    const auto upper = hlaunch::platform::windows::normalizeSearchText("\xC3\x84PFEL \xCE\x91\xCE\x92");
    const auto lower = hlaunch::platform::windows::normalizeSearchText(L"äpfel αβ");
    REQUIRE(upper.has_value());
    REQUIRE(lower.has_value());
    CHECK(*upper == *lower);
    CHECK_FALSE(hlaunch::platform::windows::normalizeSearchText(
        std::string_view{"\xC3\x28", 2}).has_value());
}

TEST_CASE("PROD-GRID-001 search handles 5000 items, empty queries and result limits")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "Large",
        }},
    };
    for (int index = 0; index < 5'000; ++index) {
        document.tabs[0].items.push_back(item(
            "item-" + std::to_string(index),
            index == 4'999 ? "Needle" : "Entry " + std::to_string(index)));
    }
    hlaunch::core::SearchIndex searchIndex{};
    searchIndex.rebuild(document, normalizeItemName);
    CHECK(searchIndex.size() == 5'000);
    CHECK(searchIndex.search(L"").empty());

    const auto query = hlaunch::platform::windows::normalizeSearchText(L"needle");
    REQUIRE(query.has_value());
    const auto results = searchIndex.search(*query, 1);
    REQUIRE(results.size() == 1);
    CHECK(results[0].itemIndex == 4'999);
}
