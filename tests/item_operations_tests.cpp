#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/data_validation.h"
#include "core/item_operations.h"
#include "platform/windows/uuid.h"

#include <ranges>

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

TEST_CASE("PROD-ITEM-001 add honors an explicitly requested empty Grid slot")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {{
                .id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                .name = "Existing",
                .gridSlot = 2U,
            }},
        }},
    };

    const auto added = hlaunch::core::addItem(
        document, 0U,
        hlaunch::core::LaunchItem{
            .id = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
            .name = "Added",
        },
        17U);

    REQUIRE(added.has_value());
    REQUIRE(document.tabs.front().items.size() == 2U);
    CHECK(hlaunch::core::gridSlotForItem(document.tabs.front(), added->itemIndex) == 17U);
    CHECK(document.tabs.front().items.front().gridSlot == 2U);
}

TEST_CASE("PROD-GRID-001 expanding columns preserves two dimensional coordinates")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {
                {.name = "A", .gridSlot = 0U},
                {.name = "B", .gridSlot = 4U},
                {.name = "C", .gridSlot = 5U},
                {.name = "D", .gridSlot = 9U},
            },
        }},
    };

    REQUIRE(hlaunch::core::reflowGridColumns(document, 5U, 7U).has_value());
    const auto slotFor = [&document](const std::string_view name) {
        const auto& items = document.tabs.front().items;
        return std::ranges::find(items, name, &hlaunch::core::LaunchItem::name)->gridSlot;
    };
    CHECK(slotFor("A") == 0U);
    CHECK(slotFor("B") == 4U);
    CHECK(slotFor("C") == 7U);
    CHECK(slotFor("D") == 11U);
}

TEST_CASE("PROD-GRID-001 shrinking columns moves only clipped items forward locally")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {
                {.name = "A", .gridSlot = 0U},
                {.name = "B clipped row zero", .gridSlot = 4U},
                {.name = "C", .gridSlot = 5U},
                {.name = "D", .gridSlot = 7U},
                {.name = "E clipped row one", .gridSlot = 9U},
                {.name = "F", .gridSlot = 11U},
            },
        }},
    };

    REQUIRE(hlaunch::core::reflowGridColumns(document, 5U, 3U).has_value());
    const auto slotFor = [&document](const std::string_view name) {
        const auto& items = document.tabs.front().items;
        return std::ranges::find(items, name, &hlaunch::core::LaunchItem::name)->gridSlot;
    };
    CHECK(slotFor("A") == 0U);
    CHECK(slotFor("C") == 3U);
    CHECK(slotFor("B clipped row zero") == 4U);
    CHECK(slotFor("D") == 5U);
    CHECK(slotFor("E clipped row one") == 6U);
    CHECK(slotFor("F") == 7U);
    CHECK_FALSE(hlaunch::core::itemIndexAtGridSlot(document.tabs.front(), 1U));
    CHECK_FALSE(hlaunch::core::itemIndexAtGridSlot(document.tabs.front(), 2U));
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

TEST_CASE("PROD-GRID-001 move reorders items using the final target index")
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

    const auto moved = hlaunch::core::moveItem(document, {0, 0}, 0, 2);

    REQUIRE(moved.has_value());
    CHECK(*moved == hlaunch::core::ItemLocation{0, 2});
    REQUIRE(document.tabs[0].items.size() == 3);
    CHECK(document.tabs[0].items[0].id == "second");
    CHECK(document.tabs[0].items[1].id == "third");
    CHECK(document.tabs[0].items[2].id == "first");
}

TEST_CASE("PROD-GRID-001 move transfers an item to an exact position in another tab")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {
            hlaunch::core::Tab{
                .id = "11111111-1111-4111-8111-111111111111",
                .name = "One",
                .items = {
                    {.id = "first", .name = "First"},
                    {.id = "second", .name = "Second"},
                },
            },
            hlaunch::core::Tab{
                .id = "22222222-2222-4222-8222-222222222222",
                .name = "Two",
                .items = {
                    {.id = "third", .name = "Third"},
                },
            },
        },
    };

    const auto moved = hlaunch::core::moveItem(document, {0, 1}, 1, 0);

    REQUIRE(moved.has_value());
    CHECK(*moved == hlaunch::core::ItemLocation{1, 0});
    REQUIRE(document.tabs[0].items.size() == 1);
    CHECK(document.tabs[0].items[0].id == "first");
    REQUIRE(document.tabs[1].items.size() == 2);
    CHECK(document.tabs[1].items[0].id == "second");
    CHECK(document.tabs[1].items[1].id == "third");
}

TEST_CASE("PROD-GRID-001 move to an empty Grid slot preserves the gap")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {{.id = "first", .name = "First"}},
        }},
    };

    const auto moved = hlaunch::core::moveItem(document, {0, 0}, 0, 7);

    REQUIRE(moved.has_value());
    REQUIRE(document.tabs[0].items.size() == 1);
    CHECK(document.tabs[0].items[0].id == "first");
    CHECK(document.tabs[0].items[0].gridSlot == 7U);
    CHECK(hlaunch::core::gridSlotExtent(document.tabs[0]) == 8U);
}

TEST_CASE("PROD-GRID-001 invalid Grid slot leaves the document unchanged")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {{.id = "first", .name = "First"}},
        }},
    };
    const auto original = document;

    const auto moved = hlaunch::core::moveItem(
        document, {0, 0}, 0, hlaunch::core::maximumGridSlotsPerTab);

    REQUIRE_FALSE(moved.has_value());
    CHECK(moved.error() == hlaunch::core::ItemMutationError::InvalidTargetIndex);
    CHECK(document == original);
}

TEST_CASE("PROD-DROP-001 batch import starts at the dropped Grid slot")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {
                {.id = "existing-first", .name = "Existing first", .target = "first.exe", .gridSlot = 0},
                {.id = "existing-later", .name = "Existing later", .target = "later.exe", .gridSlot = 4},
            },
        }},
    };
    std::vector<hlaunch::core::LaunchItem> imported{
        {.id = "imported-first", .name = "Imported first", .target = "imported-first.exe"},
        {.id = "imported-second", .name = "Imported second", .target = "imported-second.exe"},
    };

    const auto result = hlaunch::core::addImportedItems(
        document, 0, std::move(imported), false, 2U);

    REQUIRE(result.has_value());
    REQUIRE(result->added.size() == 2U);
    CHECK(document.tabs[0].items[0].gridSlot == 0U);
    CHECK(document.tabs[0].items[1].id == "imported-first");
    CHECK(document.tabs[0].items[1].gridSlot == 2U);
    CHECK(document.tabs[0].items[2].id == "imported-second");
    CHECK(document.tabs[0].items[2].gridSlot == 3U);
    CHECK(document.tabs[0].items[3].id == "existing-later");
    CHECK(document.tabs[0].items[3].gridSlot == 4U);
}

TEST_CASE("PROD-DROP-001 occupied Grid slots shift forward without reordering")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {
                {.id = "existing-a", .name = "A", .target = "a.exe", .gridSlot = 2},
                {.id = "existing-b", .name = "B", .target = "b.exe", .gridSlot = 3},
                {.id = "existing-c", .name = "C", .target = "c.exe", .gridSlot = 5},
            },
        }},
    };

    const auto result = hlaunch::core::addImportedItems(
        document,
        0,
        {{.id = "imported", .name = "Imported", .target = "imported.exe"}},
        false,
        2U);

    REQUIRE(result.has_value());
    REQUIRE(document.tabs[0].items.size() == 4U);
    CHECK(document.tabs[0].items[0].id == "imported");
    CHECK(document.tabs[0].items[0].gridSlot == 2U);
    CHECK(document.tabs[0].items[1].id == "existing-a");
    CHECK(document.tabs[0].items[1].gridSlot == 3U);
    CHECK(document.tabs[0].items[2].id == "existing-b");
    CHECK(document.tabs[0].items[2].gridSlot == 4U);
    CHECK(document.tabs[0].items[3].id == "existing-c");
    CHECK(document.tabs[0].items[3].gridSlot == 5U);
}

TEST_CASE("PROD-GRID-001 move tab preserves the complete tab and final order")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {
            {.id = "first", .name = "First"},
            {
                .id = "second",
                .name = "Second",
                .items = {{.id = "item", .name = "Item", .target = "item.exe"}},
            },
            {.id = "third", .name = "Third"},
        },
    };

    const auto moved = hlaunch::core::moveTab(document, 1, 0);

    REQUIRE(moved.has_value());
    CHECK(*moved == 0U);
    REQUIRE(document.tabs.size() == 3U);
    CHECK(document.tabs[0].id == "second");
    REQUIRE(document.tabs[0].items.size() == 1U);
    CHECK(document.tabs[0].items[0].id == "item");
    CHECK(document.tabs[1].id == "first");
    CHECK(document.tabs[2].id == "third");
}

TEST_CASE("PROD-GRID-001 invalid tab move is atomic")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {
            {.id = "first", .name = "First"},
            {.id = "second", .name = "Second"},
        },
    };
    const auto original = document;

    const auto moved = hlaunch::core::moveTab(document, 0, 2);

    REQUIRE_FALSE(moved.has_value());
    CHECK(moved.error() == hlaunch::core::ItemMutationError::InvalidTab);
    CHECK(document == original);
}
