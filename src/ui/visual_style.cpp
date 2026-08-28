#include "ui/visual_style.h"

#include "ui/system_appearance.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace hlaunch::ui {
namespace {

constexpr LauncherPalette builtInLauncherPalette{
    .background = 0x373737,
    .surface = 0x515151,
    .elevated = 0x18243A,
    .text = 0xF8FAFC,
    .textMuted = 0x94A3B8,
    .accent = 0x2DD4BF,
    .tabBackground = 0x252525,
    .tabHover = 0x414141,
    .tabIndicator = 0x0D7FD9,
    .inputBackground = 0x303030,
    .inputBorder = 0x626262,
    .itemHighlight = 0xFFFFFF,
    .focus = 0x0D7FD9,
    .border = 0x52627D,
    .danger = 0xEF4444,
};

std::uint32_t systemColor(const int index) noexcept
{
    const COLORREF color = GetSysColor(index);
    return (static_cast<std::uint32_t>(GetRValue(color)) << 16U)
        | (static_cast<std::uint32_t>(GetGValue(color)) << 8U)
        | static_cast<std::uint32_t>(GetBValue(color));
}

} // namespace

LauncherPalette launcherPalette(const bool highContrast) noexcept
{
    if (!highContrast) {
        return builtInLauncherPalette;
    }
    return {
        .background = systemColor(COLOR_WINDOW),
        .surface = systemColor(COLOR_WINDOW),
        .elevated = systemColor(COLOR_BTNFACE),
        .text = systemColor(COLOR_WINDOWTEXT),
        .textMuted = systemColor(COLOR_GRAYTEXT),
        .accent = systemColor(COLOR_HIGHLIGHT),
        .tabBackground = systemColor(COLOR_WINDOW),
        .tabHover = systemColor(COLOR_BTNFACE),
        .tabIndicator = systemColor(COLOR_HIGHLIGHT),
        .inputBackground = systemColor(COLOR_WINDOW),
        .inputBorder = systemColor(COLOR_WINDOWFRAME),
        .itemHighlight = systemColor(COLOR_HIGHLIGHT),
        .focus = systemColor(COLOR_HIGHLIGHT),
        .border = systemColor(COLOR_WINDOWFRAME),
        .danger = systemColor(COLOR_HIGHLIGHT),
    };
}

LauncherChromeIconTone launcherChromeIconTone(
    const LauncherChromeIcon icon,
    const bool hovered,
    const bool windowPinned) noexcept
{
    if (hovered) {
        return icon == LauncherChromeIcon::Close
            ? LauncherChromeIconTone::Danger
            : LauncherChromeIconTone::Text;
    }
    if (icon == LauncherChromeIcon::Pin && windowPinned) {
        return LauncherChromeIconTone::Accent;
    }
    return LauncherChromeIconTone::Muted;
}

COLORREF toColorRef(const std::uint32_t rgb) noexcept
{
    return RGB((rgb >> 16U) & 0xFFU, (rgb >> 8U) & 0xFFU, rgb & 0xFFU);
}

void applyNativeWindowStyle(
    const HWND window,
    const bool dark,
    const bool preferSmallCorners) noexcept
{
    if (!window) {
        return;
    }
    const BOOL immersiveDark = dark && !isHighContrastEnabled() ? TRUE : FALSE;
    static_cast<void>(DwmSetWindowAttribute(
        window, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersiveDark, sizeof(immersiveDark)));
    const DWM_WINDOW_CORNER_PREFERENCE corners = preferSmallCorners
        ? DWMWCP_ROUNDSMALL
        : DWMWCP_ROUND;
    static_cast<void>(DwmSetWindowAttribute(
        window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners)));
}

void applyNativeControlStyle(const HWND control, const bool dark) noexcept
{
    if (!control) {
        return;
    }
    static_cast<void>(SetWindowTheme(
        control, dark && !isHighContrastEnabled() ? L"DarkMode_Explorer" : L"Explorer", nullptr));
}

} // namespace hlaunch::ui
