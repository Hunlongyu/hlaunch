#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/data_validation.h"
#include "core/item_operations.h"
#include "platform/windows/uuid.h"

TEST_CASE("PROD-ITEM-001 add and edit preserve identity and usage statistics")
{
    hlaunch::core::ItemsDocument document{
        .tabs =
            {
                hlaunch::core::Tab{.id = "11111111-1111-4111-8111-111111111111", .name = "One"},
                hlaunch::core::Tab{.id = "22222222-2222-4222-8222-222222222222", .name = "Two"},
            },
    };
    auto id = hlaunch::platform::windows::createUuidV4();
    REQUIRE(id.has_value());
    REQUIRE(hlaunch::core::isValidUuidV4(*id));

    auto added = hlaunch::core::addItem(document, 0,
                                        hlaunch::core::LaunchItem{
                                            .id = *id,
                                            .name = "Original",
                                            .target = "original.exe",
                                            .launchCount = 7,
                                            .lastLaunchedAt = "2026-08-26T00:00:00Z",
                                        });
    REQUIRE(added.has_value());
    CHECK(*added == hlaunch::core::ItemLocation{0, 0});

    auto updated = hlaunch::core::updateItem(
        document, *added, 1, hlaunch::core::LaunchItem{.name = "Updated", .target = "updated.exe"});
    REQUIRE(updated.has_value());
    CHECK(*updated == hlaunch::core::ItemLocation{1, 0});
    REQUIRE(document.tabs[0].items.empty());
    REQUIRE(document.tabs[1].items.size() == 1);
    CHECK(document.tabs[1].items[0].id == *id);
    CHECK(document.tabs[1].items[0].launchCount == 7);
    CHECK(document.tabs[1].items[0].lastLaunchedAt == "2026-08-26T00:00:00Z");
}

TEST_CASE("PROD-ITEM-001 mutations reject invalid locations and duplicate IDs")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {hlaunch::core::LaunchItem{.id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"}},
        }},
    };
    const auto duplicate = hlaunch::core::addItem(document, 0,
                                                  hlaunch::core::LaunchItem{
                                                      .id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                                                  });
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error() == hlaunch::core::ItemMutationError::DuplicateId);

    const auto invalid =
        hlaunch::core::updateItem(document, hlaunch::core::ItemLocation{0, 2}, 0, {});
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error() == hlaunch::core::ItemMutationError::InvalidItem);
}

TEST_CASE("PROD-DROP-001 exact launch duplicates ignore presentation fields")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {hlaunch::core::LaunchItem{
                .id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                .type = hlaunch::core::ItemType::Application,
                .name = "Original",
                .target = "C:\\Tools\\tool.exe",
                .arguments = {"--work"},
                .icon = "first.ico",
            }},
        }},
    };

    auto duplicate = document.tabs.front().items.front();
    duplicate.id = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    duplicate.type = hlaunch::core::ItemType::File;
    duplicate.name = "Different name";
    duplicate.icon = "second.ico";
    CHECK(hlaunch::core::hasExactLaunchDuplicate(document, duplicate));

    duplicate.arguments.push_back("--different");
    CHECK_FALSE(hlaunch::core::hasExactLaunchDuplicate(document, duplicate));
}

TEST_CASE("PROD-DROP-001 batch import preserves order and skips exact duplicates")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {hlaunch::core::LaunchItem{
                .id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                .name = "Existing",
                .target = "existing.exe",
            }},
        }},
    };
    std::vector<hlaunch::core::LaunchItem> imported{
        {.id = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", .name = "Duplicate", .target = "existing.exe"},
        {.id = "cccccccc-cccc-4ccc-8ccc-cccccccccccc", .name = "First", .target = "first.exe"},
        {.id = "dddddddd-dddd-4ddd-8ddd-dddddddddddd", .name = "Second", .target = "second.exe"},
    };

    const auto result = hlaunch::core::addImportedItems(document, 0, std::move(imported), false);
    REQUIRE(result.has_value());
    CHECK(result->skippedDuplicates == 1U);
    REQUIRE(result->added.size() == 2U);
    CHECK(document.tabs.front().items[1].name == "First");
    CHECK(document.tabs.front().items[2].name == "Second");
}

TEST_CASE("PROD-ITEM-001 remove returns the item and preserves sibling order")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {
                {.id = "first", .name = "First"},
                {.id = "second", .name = "Second"},
                {.id = "third", .name = "Third"},
            },
        }},
    };

    const auto removed = hlaunch::core::removeItem(document, {0, 1});

    REQUIRE(removed.has_value());
    CHECK(removed->id == "second");
    REQUIRE(document.tabs[0].items.size() == 2);
    CHECK(document.tabs[0].items[0].id == "first");
    CHECK(document.tabs[0].items[1].id == "third");

    const auto invalidTab = hlaunch::core::removeItem(document, {1, 0});
    REQUIRE_FALSE(invalidTab.has_value());
    CHECK(invalidTab.error() == hlaunch::core::ItemMutationError::InvalidTab);
    const auto invalidItem = hlaunch::core::removeItem(document, {0, 2});
    REQUIRE_FALSE(invalidItem.has_value());
    CHECK(invalidItem.error() == hlaunch::core::ItemMutationError::InvalidItem);
}
