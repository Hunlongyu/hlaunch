#pragma once

#include <Windows.h>

#include <string_view>

namespace hlaunch::ui {

[[nodiscard]] bool copyUnicodeTextToClipboard(
    HWND owner,
    std::wstring_view text) noexcept;

} // namespace hlaunch::ui
