#pragma once

#include <Windows.h>

#include <cstdint>

namespace hlaunch::ui {

struct LauncherPalette {
    std::uint32_t background{};
    std::uint32_t surface{};
    std::uint32_t elevated{};
    std::uint32_t text{};
    std::uint32_t textMuted{};
    std::uint32_t accent{};
    std::uint32_t tabBackground{};
    std::uint32_t tabHover{};
    std::uint32_t tabIndicator{};
    std::uint32_t inputBackground{};
    std::uint32_t inputBorder{};
    std::uint32_t itemHighlight{};
    std::uint32_t focus{};
    std::uint32_t border{};
    std::uint32_t danger{};
    float backgroundAlpha{1.0F};
    float surfaceAlpha{1.0F};
    float elevatedAlpha{1.0F};
    float textAlpha{1.0F};
    float textMutedAlpha{1.0F};
    float accentAlpha{1.0F};
    float tabBackgroundAlpha{1.0F};
    float tabHoverAlpha{1.0F};
    float tabIndicatorAlpha{1.0F};
    float borderAlpha{1.0F};

    bool operator==(const LauncherPalette&) const = default;
};

enum class LauncherChromeIcon {
    Menu,
    Pin,
    Close,
};

enum class LauncherChromeIconTone {
    Muted,
    Text,
    Accent,
    Danger,
};

[[nodiscard]] LauncherPalette launcherPalette(bool highContrast) noexcept;
[[nodiscard]] LauncherChromeIconTone launcherChromeIconTone(
    LauncherChromeIcon icon,
    bool hovered,
    bool windowPinned) noexcept;
[[nodiscard]] COLORREF toColorRef(std::uint32_t rgb) noexcept;
void applyNativeWindowStyle(
    HWND window,
    bool dark,
    bool preferSmallCorners = false) noexcept;
void applyNativeControlStyle(HWND control, bool dark) noexcept;

} // namespace hlaunch::ui
