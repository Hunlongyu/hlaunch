#pragma once

#include "core/data_model.h"

#include <cstddef>
#include <cstdint>
#include <expected>

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
    DuplicateId,
};

[[nodiscard]] std::expected<ItemLocation, ItemMutationError>
addItem(ItemsDocument &document, std::size_t targetTabIndex, LaunchItem item);

[[nodiscard]] std::expected<ItemLocation, ItemMutationError> updateItem(ItemsDocument &document,
                                                                        ItemLocation source,
                                                                        std::size_t targetTabIndex,
                                                                        LaunchItem replacement);

} // namespace hlaunch::core
