#include "core/item_operations.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
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

bool hasSameLaunchProperties(const LaunchItem &left, const LaunchItem &right) noexcept
{
    return left.target == right.target && left.arguments == right.arguments &&
           left.workingDirectory == right.workingDirectory &&
           left.runAsAdministrator == right.runAsAdministrator;
}

void sortByGridSlot(Tab &tab)
{
    std::ranges::stable_sort(tab.items, {}, [](const LaunchItem &item) {
        return item.gridSlot.value_or(std::numeric_limits<std::uint32_t>::max());
    });
}

std::expected<std::size_t, ItemMutationError>
insertAtGridSlot(Tab &tab, LaunchItem item, const std::size_t requestedGridSlot)
{
    if (requestedGridSlot >= maximumGridSlotsPerTab)
    {
        return std::unexpected(ItemMutationError::InvalidTargetIndex);
    }

    std::size_t freeSlot = requestedGridSlot;
    while (freeSlot < maximumGridSlotsPerTab && itemIndexAtGridSlot(tab, freeSlot))
    {
        ++freeSlot;
    }
    if (freeSlot >= maximumGridSlotsPerTab)
    {
        return std::unexpected(ItemMutationError::InvalidTargetIndex);
    }

    const auto insertedId = item.id;
    LaunchItem displaced = std::move(item);
    for (std::size_t slot = requestedGridSlot; slot <= freeSlot; ++slot)
    {
        displaced.gridSlot = static_cast<std::uint32_t>(slot);
        const auto occupied = itemIndexAtGridSlot(tab, slot);
        if (!occupied)
        {
            tab.items.push_back(std::move(displaced));
            break;
        }
        std::swap(displaced, tab.items[*occupied]);
    }

    sortByGridSlot(tab);
    const auto inserted = std::ranges::find(tab.items, insertedId, &LaunchItem::id);
    if (inserted == tab.items.end())
    {
        return std::unexpected(ItemMutationError::InvalidItem);
    }
    return static_cast<std::size_t>(std::distance(tab.items.begin(), inserted));
}

} // namespace

bool hasExactLaunchDuplicate(const ItemsDocument &document,
                             const LaunchItem &candidate) noexcept
{
    return std::ranges::any_of(document.tabs, [&candidate](const Tab &tab) {
        return std::ranges::any_of(tab.items, [&candidate](const LaunchItem &item) {
            return hasSameLaunchProperties(item, candidate);
        });
    });
}

void normalizeGridSlots(Tab &tab)
{
    std::unordered_set<std::uint32_t> occupied{};
    for (auto &item : tab.items)
    {
        if (!item.gridSlot || *item.gridSlot >= maximumGridSlotsPerTab
            || !occupied.insert(*item.gridSlot).second)
        {
            item.gridSlot.reset();
        }
    }

    std::uint32_t nextSlot{};
    for (auto &item : tab.items)
    {
        if (item.gridSlot)
        {
            continue;
        }
        while (occupied.contains(nextSlot))
        {
            ++nextSlot;
        }
        item.gridSlot = nextSlot;
        occupied.insert(nextSlot);
    }
    sortByGridSlot(tab);
}

void normalizeGridSlots(ItemsDocument &document)
{
    for (auto &tab : document.tabs)
    {
        normalizeGridSlots(tab);
    }
}

std::optional<std::size_t>
itemIndexAtGridSlot(const Tab &tab, const std::size_t gridSlot) noexcept
{
    const auto found = std::ranges::find_if(tab.items, [gridSlot](const LaunchItem &item) {
        return item.gridSlot && *item.gridSlot == gridSlot;
    });
    if (found == tab.items.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(tab.items.begin(), found));
}

std::optional<std::size_t>
gridSlotForItem(const Tab &tab, const std::size_t itemIndex) noexcept
{
    if (itemIndex >= tab.items.size() || !tab.items[itemIndex].gridSlot)
    {
        return std::nullopt;
    }
    return *tab.items[itemIndex].gridSlot;
}

std::size_t gridSlotExtent(const Tab &tab) noexcept
{
    std::size_t extent{};
    for (const auto &item : tab.items)
    {
        if (item.gridSlot)
        {
            extent = std::max(extent, static_cast<std::size_t>(*item.gridSlot) + 1U);
        }
    }
    return extent;
}

std::expected<void, ItemMutationError>
reflowGridColumns(ItemsDocument &document,
                  const std::size_t oldColumnCount,
                  const std::size_t newColumnCount)
{
    if (oldColumnCount == 0U || newColumnCount == 0U) {
        return std::unexpected(ItemMutationError::InvalidTargetIndex);
    }
    if (oldColumnCount == newColumnCount) {
        return {};
    }

    auto updated = document;
    normalizeGridSlots(updated);
    for (auto &tab : updated.tabs) {
        std::unordered_set<std::uint32_t> occupied{};
        std::vector<std::size_t> overflowItems{};
        overflowItems.reserve(tab.items.size());

        for (std::size_t index = 0; index < tab.items.size(); ++index) {
            auto &item = tab.items[index];
            const auto oldSlot = static_cast<std::size_t>(*item.gridSlot);
            const auto oldRow = oldSlot / oldColumnCount;
            const auto oldColumn = oldSlot % oldColumnCount;
            if (oldColumn >= newColumnCount) {
                overflowItems.push_back(index);
                continue;
            }

            const auto newSlot = oldRow * newColumnCount + oldColumn;
            if (newSlot >= maximumGridSlotsPerTab) {
                return std::unexpected(ItemMutationError::InvalidTargetIndex);
            }
            item.gridSlot = static_cast<std::uint32_t>(newSlot);
            occupied.insert(*item.gridSlot);
        }

        std::size_t previousOverflowSlot{};
        bool hasPreviousOverflow{};
        for (const auto itemIndex : overflowItems) {
            auto &item = tab.items[itemIndex];
            const auto oldSlot = static_cast<std::size_t>(*item.gridSlot);
            const auto oldRow = oldSlot / oldColumnCount;
            std::size_t candidate = (oldRow + 1U) * newColumnCount;
            if (hasPreviousOverflow) {
                candidate = std::max(candidate, previousOverflowSlot + 1U);
            }
            while (candidate < maximumGridSlotsPerTab
                   && occupied.contains(static_cast<std::uint32_t>(candidate))) {
                ++candidate;
            }
            if (candidate >= maximumGridSlotsPerTab) {
                return std::unexpected(ItemMutationError::InvalidTargetIndex);
            }
            item.gridSlot = static_cast<std::uint32_t>(candidate);
            occupied.insert(*item.gridSlot);
            previousOverflowSlot = candidate;
            hasPreviousOverflow = true;
        }
        sortByGridSlot(tab);
    }

    document = std::move(updated);
    return {};
}

std::expected<ItemLocation, ItemMutationError>
addItem(ItemsDocument &document, const std::size_t targetTabIndex, LaunchItem item,
        const std::optional<std::size_t> targetGridSlot)
{
    if (targetTabIndex >= document.tabs.size())
    {
        return std::unexpected(ItemMutationError::InvalidTab);
    }
    if (containsItemId(document, item.id))
    {
        return std::unexpected(ItemMutationError::DuplicateId);
    }
    auto updated = document;
    auto &tab = updated.tabs[targetTabIndex];
    normalizeGridSlots(tab);
    const auto requestedGridSlot = targetGridSlot.value_or(gridSlotExtent(tab));
    const auto inserted = insertAtGridSlot(tab, std::move(item), requestedGridSlot);
    if (!inserted)
    {
        return std::unexpected(inserted.error());
    }
    document = std::move(updated);
    return ItemLocation{targetTabIndex, *inserted};
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
    auto updated = document;
    normalizeGridSlots(updated);
    auto &sourceItems = updated.tabs[source.tabIndex].items;
    if (source.itemIndex >= sourceItems.size())
    {
        return std::unexpected(ItemMutationError::InvalidItem);
    }
    const auto stableId = sourceItems[source.itemIndex].id;
    const auto launchCount = sourceItems[source.itemIndex].launchCount;
    const auto lastLaunchedAt = sourceItems[source.itemIndex].lastLaunchedAt;
    const auto gridSlot = sourceItems[source.itemIndex].gridSlot;
    replacement.id = stableId;
    replacement.launchCount = launchCount;
    replacement.lastLaunchedAt = lastLaunchedAt;
    replacement.gridSlot = gridSlot;

    if (source.tabIndex == targetTabIndex)
    {
        sourceItems[source.itemIndex] = std::move(replacement);
        document = std::move(updated);
        return source;
    }

    sourceItems.erase(sourceItems.begin() + static_cast<std::ptrdiff_t>(source.itemIndex));
    auto &targetTab = updated.tabs[targetTabIndex];
    const auto targetGridSlot = gridSlotExtent(targetTab);
    const auto inserted = insertAtGridSlot(targetTab, std::move(replacement), targetGridSlot);
    if (!inserted)
    {
        return std::unexpected(inserted.error());
    }
    document = std::move(updated);
    return ItemLocation{targetTabIndex, *inserted};
}

std::expected<LaunchItem, ItemMutationError>
removeItem(ItemsDocument &document, const ItemLocation source)
{
    if (source.tabIndex >= document.tabs.size())
    {
        return std::unexpected(ItemMutationError::InvalidTab);
    }
    normalizeGridSlots(document.tabs[source.tabIndex]);
    auto &items = document.tabs[source.tabIndex].items;
    if (source.itemIndex >= items.size())
    {
        return std::unexpected(ItemMutationError::InvalidItem);
    }

    LaunchItem removed = std::move(items[source.itemIndex]);
    items.erase(items.begin() + static_cast<std::ptrdiff_t>(source.itemIndex));
    return removed;
}

std::expected<ItemLocation, ItemMutationError>
moveItem(ItemsDocument &document, const ItemLocation source,
         const std::size_t targetTabIndex, const std::size_t targetItemIndex)
{
    if (source.tabIndex >= document.tabs.size() || targetTabIndex >= document.tabs.size())
    {
        return std::unexpected(ItemMutationError::InvalidTab);
    }

    if (targetItemIndex >= maximumGridSlotsPerTab)
    {
        return std::unexpected(ItemMutationError::InvalidTargetIndex);
    }

    auto updated = document;
    normalizeGridSlots(updated);
    auto &sourceItems = updated.tabs[source.tabIndex].items;
    if (source.itemIndex >= sourceItems.size())
    {
        return std::unexpected(ItemMutationError::InvalidItem);
    }

    const auto sourceGridSlot = sourceItems[source.itemIndex].gridSlot.value();
    if (source.tabIndex == targetTabIndex && sourceGridSlot == targetItemIndex)
    {
        return source;
    }

    const auto movedId = sourceItems[source.itemIndex].id;
    if (source.tabIndex == targetTabIndex)
    {
        if (itemIndexAtGridSlot(updated.tabs[targetTabIndex], targetItemIndex))
        {
            for (auto &candidate : sourceItems)
            {
                if (candidate.id == movedId || !candidate.gridSlot)
                {
                    continue;
                }
                if (sourceGridSlot < targetItemIndex
                    && *candidate.gridSlot > sourceGridSlot
                    && *candidate.gridSlot <= targetItemIndex)
                {
                    --*candidate.gridSlot;
                }
                else if (sourceGridSlot > targetItemIndex
                         && *candidate.gridSlot >= targetItemIndex
                         && *candidate.gridSlot < sourceGridSlot)
                {
                    ++*candidate.gridSlot;
                }
            }
        }
        sourceItems[source.itemIndex].gridSlot = static_cast<std::uint32_t>(targetItemIndex);
        sortByGridSlot(updated.tabs[targetTabIndex]);
    }
    else
    {
        LaunchItem moved = std::move(sourceItems[source.itemIndex]);
        sourceItems.erase(sourceItems.begin() + static_cast<std::ptrdiff_t>(source.itemIndex));
        const auto inserted = insertAtGridSlot(
            updated.tabs[targetTabIndex], std::move(moved), targetItemIndex);
        if (!inserted)
        {
            return std::unexpected(inserted.error());
        }
    }

    const auto &targetItems = updated.tabs[targetTabIndex].items;
    const auto moved = std::ranges::find(targetItems, movedId, &LaunchItem::id);
    if (moved == targetItems.end())
    {
        return std::unexpected(ItemMutationError::InvalidItem);
    }
    const auto resultIndex = static_cast<std::size_t>(
        std::distance(targetItems.begin(), moved));
    document = std::move(updated);
    return ItemLocation{targetTabIndex, resultIndex};
}

std::expected<std::size_t, ItemMutationError>
moveTab(ItemsDocument &document, const std::size_t sourceTabIndex,
        const std::size_t targetTabIndex)
{
    if (sourceTabIndex >= document.tabs.size() || targetTabIndex >= document.tabs.size())
    {
        return std::unexpected(ItemMutationError::InvalidTab);
    }
    if (sourceTabIndex == targetTabIndex)
    {
        return sourceTabIndex;
    }

    Tab moved = std::move(document.tabs[sourceTabIndex]);
    document.tabs.erase(document.tabs.begin() + static_cast<std::ptrdiff_t>(sourceTabIndex));
    document.tabs.insert(
        document.tabs.begin() + static_cast<std::ptrdiff_t>(targetTabIndex),
        std::move(moved));
    return targetTabIndex;
}

std::expected<BatchItemMutationResult, ItemMutationError>
addImportedItems(ItemsDocument &document, const std::size_t targetTabIndex,
                 std::vector<LaunchItem> items, const bool allowExactDuplicates,
                 const std::optional<std::size_t> targetGridSlot)
{
    if (targetTabIndex >= document.tabs.size())
    {
        return std::unexpected(ItemMutationError::InvalidTab);
    }

    auto updated = document;
    normalizeGridSlots(updated);
    const auto firstGridSlot = targetGridSlot.value_or(
        gridSlotExtent(updated.tabs[targetTabIndex]));
    if (firstGridSlot >= maximumGridSlotsPerTab)
    {
        return std::unexpected(ItemMutationError::InvalidTargetIndex);
    }

    BatchItemMutationResult result{};
    result.added.reserve(items.size());
    std::vector<std::string> addedIds{};
    addedIds.reserve(items.size());
    for (auto &item : items)
    {
        if (!allowExactDuplicates && hasExactLaunchDuplicate(updated, item))
        {
            ++result.skippedDuplicates;
            continue;
        }
        if (containsItemId(updated, item.id))
        {
            return std::unexpected(ItemMutationError::DuplicateId);
        }
        const auto requestedGridSlot = firstGridSlot + addedIds.size();
        if (requestedGridSlot >= maximumGridSlotsPerTab)
        {
            return std::unexpected(ItemMutationError::InvalidTargetIndex);
        }
        addedIds.push_back(item.id);
        const auto inserted = insertAtGridSlot(
            updated.tabs[targetTabIndex], std::move(item), requestedGridSlot);
        if (!inserted)
        {
            return std::unexpected(inserted.error());
        }
    }
    for (const auto &id : addedIds)
    {
        const auto &targetItems = updated.tabs[targetTabIndex].items;
        const auto found = std::ranges::find(targetItems, id, &LaunchItem::id);
        result.added.push_back(ItemLocation{
            targetTabIndex,
            static_cast<std::size_t>(std::distance(targetItems.begin(), found)),
        });
    }
    document = std::move(updated);
    return result;
}

} // namespace hlaunch::core
