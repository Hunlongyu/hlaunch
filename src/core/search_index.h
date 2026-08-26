#pragma once

#include "core/data_model.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlaunch::core {

enum class SearchMatchKind : std::uint8_t {
    Exact,
    Prefix,
    Substring,
    Fuzzy,
};

struct SearchResult {
    std::size_t tabIndex{};
    std::size_t itemIndex{};
    SearchMatchKind matchKind{SearchMatchKind::Fuzzy};
    std::size_t matchDistance{};

    bool operator==(const SearchResult&) const = default;
};

class SearchIndex final {
public:
    using NameNormalizer =
        std::function<std::optional<std::wstring>(std::string_view)>;

    void rebuild(const ItemsDocument& document, const NameNormalizer& normalizeName);

    [[nodiscard]] std::vector<SearchResult> search(
        std::wstring_view normalizedQuery,
        std::size_t maximumResults = 25) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Entry {
        std::size_t tabIndex{};
        std::size_t itemIndex{};
        std::wstring normalizedName{};
        std::string stableId{};
        std::uint64_t launchCount{};
        std::string lastLaunchedAt{};
    };

    std::vector<Entry> entries_{};
};

} // namespace hlaunch::core
