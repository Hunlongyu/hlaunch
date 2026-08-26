#include "core/search_index.h"

#include <algorithm>

namespace hlaunch::core {
namespace {

struct Match {
    SearchMatchKind kind{SearchMatchKind::Fuzzy};
    std::size_t distance{};
};

std::optional<Match> classifyMatch(
    const std::wstring_view text,
    const std::wstring_view query) noexcept
{
    if (text == query) {
        return Match{SearchMatchKind::Exact, 0};
    }
    if (text.starts_with(query)) {
        return Match{SearchMatchKind::Prefix, 0};
    }
    if (const auto position = text.find(query); position != std::wstring_view::npos) {
        return Match{SearchMatchKind::Substring, position};
    }

    std::size_t searchFrom{};
    std::size_t distance{};
    for (const wchar_t character : query) {
        const auto position = text.find(character, searchFrom);
        if (position == std::wstring_view::npos) {
            return std::nullopt;
        }
        distance += position - searchFrom;
        searchFrom = position + 1U;
    }
    return Match{SearchMatchKind::Fuzzy, distance};
}

} // namespace

void SearchIndex::rebuild(
    const ItemsDocument& document,
    const NameNormalizer& normalizeName)
{
    entries_.clear();
    std::size_t itemCount{};
    for (const auto& tab : document.tabs) {
        itemCount += tab.items.size();
    }
    entries_.reserve(itemCount);

    for (std::size_t tabIndex = 0; tabIndex < document.tabs.size(); ++tabIndex) {
        const auto& tab = document.tabs[tabIndex];
        for (std::size_t itemIndex = 0; itemIndex < tab.items.size(); ++itemIndex) {
            const auto& item = tab.items[itemIndex];
            const auto normalizedName = normalizeName(item.name);
            if (!normalizedName) {
                continue;
            }
            entries_.push_back(Entry{
                .tabIndex = tabIndex,
                .itemIndex = itemIndex,
                .normalizedName = *normalizedName,
                .stableId = item.id,
                .launchCount = item.launchCount,
                .lastLaunchedAt = item.lastLaunchedAt.value_or(std::string{}),
            });
        }
    }
}

std::vector<SearchResult> SearchIndex::search(
    const std::wstring_view normalizedQuery,
    const std::size_t maximumResults) const
{
    if (normalizedQuery.empty() || maximumResults == 0) {
        return {};
    }

    struct RankedResult {
        SearchResult result{};
        std::uint64_t launchCount{};
        std::string_view lastLaunchedAt{};
        std::string_view stableId{};
    };

    std::vector<RankedResult> ranked{};
    ranked.reserve(std::min(entries_.size(), maximumResults));
    for (const auto& entry : entries_) {
        const auto match = classifyMatch(entry.normalizedName, normalizedQuery);
        if (!match) {
            continue;
        }
        ranked.push_back(RankedResult{
            .result = SearchResult{
                .tabIndex = entry.tabIndex,
                .itemIndex = entry.itemIndex,
                .matchKind = match->kind,
                .matchDistance = match->distance,
            },
            .launchCount = entry.launchCount,
            .lastLaunchedAt = entry.lastLaunchedAt,
            .stableId = entry.stableId,
        });
    }

    std::ranges::sort(ranked, [](const RankedResult& left, const RankedResult& right) {
        if (left.result.matchKind != right.result.matchKind) {
            return left.result.matchKind < right.result.matchKind;
        }
        if (left.result.matchDistance != right.result.matchDistance) {
            return left.result.matchDistance < right.result.matchDistance;
        }
        if (left.launchCount != right.launchCount) {
            return left.launchCount > right.launchCount;
        }
        if (left.lastLaunchedAt != right.lastLaunchedAt) {
            return left.lastLaunchedAt > right.lastLaunchedAt;
        }
        if (left.stableId != right.stableId) {
            return left.stableId < right.stableId;
        }
        if (left.result.tabIndex != right.result.tabIndex) {
            return left.result.tabIndex < right.result.tabIndex;
        }
        return left.result.itemIndex < right.result.itemIndex;
    });
    if (ranked.size() > maximumResults) {
        ranked.resize(maximumResults);
    }

    std::vector<SearchResult> results{};
    results.reserve(ranked.size());
    for (const auto& value : ranked) {
        results.push_back(value.result);
    }
    return results;
}

std::size_t SearchIndex::size() const noexcept
{
    return entries_.size();
}

} // namespace hlaunch::core
