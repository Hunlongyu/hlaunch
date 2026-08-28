#pragma once
#include <Windows.h>
#include <vector>
namespace hlaunch::ui {
struct DialogControlLayout { HWND window{}; int x{}; int y{}; int width{}; int height{}; };
[[nodiscard]] inline int scaleDip(int value, UINT dpi) noexcept
{ return MulDiv(value, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI); }
inline void layoutDialogControls(const std::vector<DialogControlLayout>& layouts, UINT dpi) noexcept
{
    for (const auto& item : layouts) if (item.window) SetWindowPos(item.window, nullptr,
        scaleDip(item.x, dpi), scaleDip(item.y, dpi), scaleDip(item.width, dpi), scaleDip(item.height, dpi),
        SWP_NOACTIVATE | SWP_NOZORDER);
}
} // namespace hlaunch::ui
