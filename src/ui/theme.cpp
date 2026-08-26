#include "ui/theme.h"

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

} // namespace

const ThemePalette& paletteFor(const core::ThemeMode mode) noexcept
{
    return mode == core::ThemeMode::Light ? lightPalette : darkPalette;
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
        mode == core::ThemeMode::Dark ? L"DarkMode_Explorer" : L"Explorer",
        nullptr));
}

} // namespace hlaunch::ui
