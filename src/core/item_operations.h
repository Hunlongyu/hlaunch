#pragma once

#include "core/data_model.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace hlaunch::core {

inline constexpr std::size_t maximumGridSlotsPerTab = 10'000U;

struct ItemLocation
{
    std::size_t tabIndex{};
    std::size_t itemIndex{};

    bool operator==(const ItemLocation &) const = default;
};

enum class ItemMutationError : std::uint8_t
{
    InvalidTab,
    InvalidItem,
    InvalidTargetIndex,
    DuplicateId,
};

struct BatchItemMutationResult
{
    std::vector<ItemLocation> added{};
    std::size_t skippedDuplicates{};
};

[[nodiscard]] bool hasExactLaunchDuplicate(const ItemsDocument &document,
                                           const LaunchItem &candidate) noexcept;

void normalizeGridSlots(Tab &tab);
void normalizeGridSlots(ItemsDocument &document);

[[nodiscard]] std::optional<std::size_t>
itemIndexAtGridSlot(const Tab &tab, std::size_t gridSlot) noexcept;

[[nodiscard]] std::optional<std::size_t>
gridSlotForItem(const Tab &tab, std::size_t itemIndex) noexcept;

[[nodiscard]] std::size_t gridSlotExtent(const Tab &tab) noexcept;

[[nodiscard]] std::expected<void, ItemMutationError>
reflowGridColumns(ItemsDocument &document,
                  std::size_t oldColumnCount,
                  std::size_t newColumnCount);

[[nodiscard]] std::expected<ItemLocation, ItemMutationError>
addItem(ItemsDocument &document, std::size_t targetTabIndex, LaunchItem item,
        std::optional<std::size_t> targetGridSlot = std::nullopt);

[[nodiscard]] std::expected<ItemLocation, ItemMutationError> updateItem(ItemsDocument &document,
                                                                        ItemLocation source,
                                                                        std::size_t targetTabIndex,
                                                                        LaunchItem replacement);

[[nodiscard]] std::expected<LaunchItem, ItemMutationError>
removeItem(ItemsDocument &document, ItemLocation source);

[[nodiscard]] std::expected<ItemLocation, ItemMutationError>
moveItem(ItemsDocument &document, ItemLocation source, std::size_t targetTabIndex,
         std::size_t targetItemIndex);

[[nodiscard]] std::expected<std::size_t, ItemMutationError>
moveTab(ItemsDocument &document, std::size_t sourceTabIndex, std::size_t targetTabIndex);

[[nodiscard]] std::expected<BatchItemMutationResult, ItemMutationError>
addImportedItems(ItemsDocument &document, std::size_t targetTabIndex,
                 std::vector<LaunchItem> items, bool allowExactDuplicates,
                 std::optional<std::size_t> targetGridSlot = std::nullopt);

} // namespace hlaunch::core
