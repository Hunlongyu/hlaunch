#pragma once

#include "core/data_model.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace hlaunch::core {

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

[[nodiscard]] std::expected<ItemLocation, ItemMutationError>
addItem(ItemsDocument &document, std::size_t targetTabIndex, LaunchItem item);

[[nodiscard]] std::expected<ItemLocation, ItemMutationError> updateItem(ItemsDocument &document,
                                                                        ItemLocation source,
                                                                        std::size_t targetTabIndex,
                                                                        LaunchItem replacement);

[[nodiscard]] std::expected<LaunchItem, ItemMutationError>
removeItem(ItemsDocument &document, ItemLocation source);

[[nodiscard]] std::expected<ItemLocation, ItemMutationError>
moveItem(ItemsDocument &document, ItemLocation source, std::size_t targetTabIndex,
         std::size_t targetItemIndex);

[[nodiscard]] std::expected<BatchItemMutationResult, ItemMutationError>
addImportedItems(ItemsDocument &document, std::size_t targetTabIndex,
                 std::vector<LaunchItem> items, bool allowExactDuplicates);

} // namespace hlaunch::core
