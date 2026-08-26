#include "core/item_operations.h"

#include <algorithm>
#include <utility>

namespace hlaunch::core {
namespace {

bool containsItemId(const ItemsDocument &document, const std::string_view id)
{
    return std::ranges::any_of(document.tabs, [id](const Tab &tab) {
        return std::ranges::any_of(tab.items,
                                   [id](const LaunchItem &item) { return item.id == id; });
    });
}

} // namespace

std::expected<ItemLocation, ItemMutationError>
addItem(ItemsDocument &document, const std::size_t targetTabIndex, LaunchItem item)
{
    if (targetTabIndex >= document.tabs.size())
    {
        return std::unexpected(ItemMutationError::InvalidTab);
    }
    if (containsItemId(document, item.id))
    {
        return std::unexpected(ItemMutationError::DuplicateId);
    }
    auto &items = document.tabs[targetTabIndex].items;
    items.push_back(std::move(item));
    return ItemLocation{targetTabIndex, items.size() - 1U};
}

std::expected<ItemLocation, ItemMutationError> updateItem(ItemsDocument &document,
                                                          const ItemLocation source,
                                                          const std::size_t targetTabIndex,
                                                          LaunchItem replacement)
{
    if (source.tabIndex >= document.tabs.size() || targetTabIndex >= document.tabs.size())
    {
        return std::unexpected(ItemMutationError::InvalidTab);
    }
    auto &sourceItems = document.tabs[source.tabIndex].items;
    if (source.itemIndex >= sourceItems.size())
    {
        return std::unexpected(ItemMutationError::InvalidItem);
    }
    const auto stableId = sourceItems[source.itemIndex].id;
    const auto launchCount = sourceItems[source.itemIndex].launchCount;
    const auto lastLaunchedAt = sourceItems[source.itemIndex].lastLaunchedAt;
    replacement.id = stableId;
    replacement.launchCount = launchCount;
    replacement.lastLaunchedAt = lastLaunchedAt;

    if (source.tabIndex == targetTabIndex)
    {
        sourceItems[source.itemIndex] = std::move(replacement);
        return source;
    }

    sourceItems.erase(sourceItems.begin() + static_cast<std::ptrdiff_t>(source.itemIndex));
    auto &targetItems = document.tabs[targetTabIndex].items;
    targetItems.push_back(std::move(replacement));
    return ItemLocation{targetTabIndex, targetItems.size() - 1U};
}

} // namespace hlaunch::core
