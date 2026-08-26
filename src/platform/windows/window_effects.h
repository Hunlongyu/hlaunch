#pragma once

#include <Windows.h>

#include <cstdint>

namespace hlaunch::platform::windows {

enum class WindowBackdrop : std::uint8_t {
    Solid,
    Mica,
    Acrylic,
    Tabbed,
};

struct WindowEffects {
    WindowBackdrop backdrop{WindowBackdrop::Acrylic};
    std::uint8_t opacityPercent{95};

    bool operator==(const WindowEffects&) const = default;
};

struct WindowEffectsResult {
    bool systemBackdropApplied{};
    bool blurFallbackApplied{};
    bool globalOpacityApplied{};
};

[[nodiscard]] WindowEffectsResult applyWindowEffects(
    HWND window,
    const WindowEffects& effects) noexcept;

} // namespace hlaunch::platform::windows
