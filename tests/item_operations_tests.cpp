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
