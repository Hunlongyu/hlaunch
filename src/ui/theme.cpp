#include "ui/theme.h"

#include "ui/system_appearance.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace hlaunch::ui {
namespace {

constexpr ThemePalette darkPalette{
    .background = 0x0B1120,
    .surface = 0x121B2D,
    .elevated = 0x18243A,
    .text = 0xF8FAFC,
    .textMuted = 0x94A3B8,
    .accent = 0x2DD4BF,
    .border = 0x52627D,
    .danger = 0xEF4444,
};

constexpr ThemePalette lightPalette{
    .background = 0xF3F7FA,
    .surface = 0xFFFFFF,
    .elevated = 0xE8F1F4,
    .text = 0x0F172A,
    .textMuted = 0x64748B,
    .accent = 0x0D9488,
    .border = 0xCBD5E1,
    .danger = 0xDC2626,
};

std::uint32_t systemColor(const int index) noexcept
{
    const COLORREF color = GetSysColor(index);
    return (static_cast<std::uint32_t>(GetRValue(color)) << 16U)
        | (static_cast<std::uint32_t>(GetGValue(color)) << 8U)
        | static_cast<std::uint32_t>(GetBValue(color));
}

} // namespace

ThemePalette paletteFor(const core::ThemeMode mode) noexcept
{
    return paletteFor(mode, isHighContrastEnabled());
}

ThemePalette paletteFor(
    const core::ThemeMode mode,
    const bool highContrast) noexcept
{
    if (!highContrast) {
        return mode == core::ThemeMode::Light ? lightPalette : darkPalette;
    }
    return {
        .background = systemColor(COLOR_WINDOW),
        .surface = systemColor(COLOR_WINDOW),
        .elevated = systemColor(COLOR_BTNFACE),
        .text = systemColor(COLOR_WINDOWTEXT),
        .textMuted = systemColor(COLOR_GRAYTEXT),
        .accent = systemColor(COLOR_HIGHLIGHT),
        .border = systemColor(COLOR_WINDOWFRAME),
        .danger = systemColor(COLOR_HIGHLIGHT),
    };
}

COLORREF toColorRef(const std::uint32_t rgb) noexcept
{
    return RGB((rgb >> 16U) & 0xFFU, (rgb >> 8U) & 0xFFU, rgb & 0xFFU);
}

void applyNativeWindowTheme(const HWND window, const core::ThemeMode mode) noexcept
{
    if (!window) {
        return;
    }
    const BOOL dark = mode == core::ThemeMode::Dark ? TRUE : FALSE;
    static_cast<void>(DwmSetWindowAttribute(
        window,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark,
        sizeof(dark)));
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    static_cast<void>(DwmSetWindowAttribute(
        window,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &corners,
        sizeof(corners)));
}

void applyNativeControlTheme(const HWND control, const core::ThemeMode mode) noexcept
{
    if (!control) {
        return;
    }
    static_cast<void>(SetWindowTheme(
        control,
        mode == core::ThemeMode::Dark && !isHighContrastEnabled()
            ? L"DarkMode_Explorer"
            : L"Explorer",
        nullptr));
}

} // namespace hlaunch::ui
