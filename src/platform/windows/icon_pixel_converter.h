#pragma once

#include <Windows.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace hlaunch::platform::windows {

struct IconPixels
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> values{};
};

[[nodiscard]] std::optional<IconPixels> convertIconToPixels(HICON icon,
                                                            std::uint32_t pixelSize);

} // namespace hlaunch::platform::windows
