#pragma once

#include "platform/windows/icon_pixel_converter.h"

#include <cstdint>
#include <optional>
#include <string>

namespace hlaunch::platform::windows {

[[nodiscard]] std::optional<IconPixels> decodeSvgFileToPixels(
    const std::wstring& path,
    std::uint32_t pixelSize);

} // namespace hlaunch::platform::windows
