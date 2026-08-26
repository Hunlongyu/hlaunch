#include "platform/windows/window_effects.h"

#include <dwmapi.h>

#include <algorithm>

namespace hlaunch::platform::windows {
namespace {

DWM_SYSTEMBACKDROP_TYPE toSystemBackdrop(const WindowBackdrop backdrop) noexcept
{
    switch (backdrop) {
    case WindowBackdrop::Solid:
        return DWMSBT_NONE;
    case WindowBackdrop::Mica:
        return DWMSBT_MAINWINDOW;
    case WindowBackdrop::Acrylic:
        return DWMSBT_TRANSIENTWINDOW;
    case WindowBackdrop::Tabbed:
        return DWMSBT_TABBEDWINDOW;
    }
    return DWMSBT_NONE;
}

} // namespace

WindowEffectsResult applyWindowEffects(
    const HWND window,
    const WindowEffects& effects) noexcept
{
    WindowEffectsResult result{};
    if (!window) {
        return result;
    }

    const BOOL darkMode = TRUE;
    static_cast<void>(DwmSetWindowAttribute(
        window,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &darkMode,
        sizeof(darkMode)));

    const DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    static_cast<void>(DwmSetWindowAttribute(
        window,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &cornerPreference,
        sizeof(cornerPreference)));

    const COLORREF borderColor = DWMWA_COLOR_NONE;
    static_cast<void>(DwmSetWindowAttribute(
        window,
        DWMWA_BORDER_COLOR,
        &borderColor,
        sizeof(borderColor)));

    const auto backdrop = toSystemBackdrop(effects.backdrop);
    if (effects.backdrop == WindowBackdrop::Solid) {
        const MARGINS margins{};
        static_cast<void>(DwmExtendFrameIntoClientArea(window, &margins));
        static_cast<void>(DwmSetWindowAttribute(
            window,
            DWMWA_SYSTEMBACKDROP_TYPE,
            &backdrop,
            sizeof(backdrop)));
    }
    else {
        constexpr MARGINS margins{-1, -1, -1, -1};
        static_cast<void>(DwmExtendFrameIntoClientArea(window, &margins));
        result.systemBackdropApplied = SUCCEEDED(DwmSetWindowAttribute(
            window,
            DWMWA_SYSTEMBACKDROP_TYPE,
            &backdrop,
            sizeof(backdrop)));

        if (!result.systemBackdropApplied) {
            DWM_BLURBEHIND blur{};
            blur.dwFlags = DWM_BB_ENABLE;
            blur.fEnable = TRUE;
            result.blurFallbackApplied = SUCCEEDED(DwmEnableBlurBehindWindow(window, &blur));
        }
    }

    const auto opacity = std::clamp<unsigned int>(effects.opacityPercent, 30U, 100U);
    if (opacity < 100U) {
        const auto extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, extendedStyle | WS_EX_LAYERED);
        const auto alpha = static_cast<BYTE>((opacity * 255U + 50U) / 100U);
        result.globalOpacityApplied = SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA) != FALSE;
    }

    return result;
}

} // namespace hlaunch::platform::windows
