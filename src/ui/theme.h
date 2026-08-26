#pragma once

#include "core/data_model.h"

#include <Windows.h>

#include <cstdint>

namespace hlaunch::ui {

struct ThemePalette {
    std::uint32_t background{};
    std::uint32_t surface{};
    std::uint32_t elevated{};
    std::uint32_t text{};
    std::uint32_t textMuted{};
    std::uint32_t accent{};
    std::uint32_t border{};
    std::uint32_t danger{};
};

[[nodiscard]] const ThemePalette& paletteFor(core::ThemeMode mode) noexcept;
[[nodiscard]] COLORREF toColorRef(std::uint32_t rgb) noexcept;
void applyNativeWindowTheme(HWND window, core::ThemeMode mode) noexcept;
void applyNativeControlTheme(HWND control, core::ThemeMode mode) noexcept;

} // namespace hlaunch::ui
