#pragma once

#include "core/data_model.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlaunch::infrastructure::json {

inline constexpr std::size_t maxJsonDocumentBytes = 8U * 1024U * 1024U;
inline constexpr std::size_t maxJsonNestingDepth = 32U;

enum class JsonIssueCode {
    InputTooLarge,
    NestingTooDeep,
    InvalidJson,
    UnsupportedSchema,
    Validation,
};

struct JsonIssue {
    JsonIssueCode code{JsonIssueCode::Validation};
    std::string path{};
    std::string message{};

    bool operator==(const JsonIssue&) const = default;
};

template <typename Value>
struct JsonDecodeResult {
    std::optional<Value> value{};
    std::vector<JsonIssue> issues{};
    bool readOnlyProtection{};

    [[nodiscard]] bool hasValue() const noexcept
    {
        return value.has_value();
    }
};

using ConfigDecodeResult = JsonDecodeResult<core::ApplicationConfig>;
using ItemsDecodeResult = JsonDecodeResult<core::ItemsDocument>;
using JsonEncodeResult = std::expected<std::string, std::vector<JsonIssue>>;

[[nodiscard]] ConfigDecodeResult decodeConfig(std::string_view input);
[[nodiscard]] ItemsDecodeResult decodeItems(std::string_view input);
[[nodiscard]] JsonEncodeResult encodeConfig(const core::ApplicationConfig& config);
[[nodiscard]] JsonEncodeResult encodeItems(const core::ItemsDocument& document);

} // namespace hlaunch::infrastructure::json
