#include "ui/system_appearance.h"

#include <algorithm>

namespace hlaunch::ui {
namespace {

LOGFONTW systemMessageFont(const UINT dpi) noexcept
{
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    const UINT effectiveDpi = std::max(96U, dpi);
    if (SystemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS,
            sizeof(metrics),
            &metrics,
            0,
            effectiveDpi)) {
        return metrics.lfMessageFont;
    }
    metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(
            SPI_GETNONCLIENTMETRICS,
            sizeof(metrics),
            &metrics,
            0)) {
        return metrics.lfMessageFont;
    }

    LOGFONTW fallback{};
    fallback.lfHeight = -MulDiv(9, static_cast<int>(effectiveDpi), 72);
    fallback.lfWeight = FW_NORMAL;
    static_cast<void>(wcscpy_s(fallback.lfFaceName, L"Segoe UI"));
    return fallback;
}

BOOL CALLBACK applyChildFont(const HWND child, const LPARAM parameter)
{
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(parameter), TRUE);
    return TRUE;
}

} // namespace

SystemUiFont::~SystemUiFont()
{
    if (font_) {
        DeleteObject(font_);
    }
}

bool SystemUiFont::refresh(const UINT dpi) noexcept
{
    const auto logicalFont = systemMessageFont(dpi);
    const HFONT replacement = CreateFontIndirectW(&logicalFont);
    if (!replacement) {
        return false;
    }
    if (font_) {
        DeleteObject(font_);
    }
    font_ = replacement;
    return true;
}

HFONT SystemUiFont::get() const noexcept
{
    return font_;
}

std::wstring systemUiFontFamily(const UINT dpi)
{
    const auto logicalFont = systemMessageFont(dpi);
    return logicalFont.lfFaceName[0] != L'\0'
        ? std::wstring{logicalFont.lfFaceName}
        : std::wstring{L"Segoe UI"};
}

bool isHighContrastEnabled() noexcept
{
    HIGHCONTRASTW contrast{sizeof(contrast)};
    return SystemParametersInfoW(
               SPI_GETHIGHCONTRAST,
               sizeof(contrast),
               &contrast,
               0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

bool isSystemAppearanceMessage(const UINT message) noexcept
{
    return message == WM_THEMECHANGED
        || message == WM_SETTINGCHANGE
        || message == WM_SYSCOLORCHANGE
        || message == WM_FONTCHANGE
        || message == WM_DWMCOMPOSITIONCHANGED
        || message == WM_DWMCOLORIZATIONCOLORCHANGED;
}

void applySystemUiFont(const HWND window, const HFONT font) noexcept
{
    if (!window || !font) {
        return;
    }
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    EnumChildWindows(window, applyChildFont, reinterpret_cast<LPARAM>(font));
}

} // namespace hlaunch::ui
