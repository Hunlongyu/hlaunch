#pragma once

#include "platform/windows/icon_pixel_converter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace hlaunch::platform::windows {

struct IconDiskCacheOptions
{
    std::filesystem::path directory{};
    std::size_t maximumEntries{256U};
    std::uintmax_t maximumBytes{64U * 1024U * 1024U};
};

class IconDiskCache final
{
  public:
    explicit IconDiskCache(IconDiskCacheOptions options);

    [[nodiscard]] std::optional<IconPixels> load(
        std::string_view scope,
        std::string_view identity) const;
    [[nodiscard]] bool store(
        std::string_view scope,
        std::string_view identity,
        const IconPixels &pixels) const;
    void eraseScope(std::string_view scope) const;

  private:
    [[nodiscard]] std::filesystem::path cachePath(
        std::string_view scope,
        std::string_view identity) const;
    void trim() const;

    IconDiskCacheOptions options_{};
};

} // namespace hlaunch::platform::windows
