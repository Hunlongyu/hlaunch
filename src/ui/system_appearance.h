#pragma once

#include <Windows.h>

#include <string>

namespace hlaunch::ui {

class SystemUiFont final {
public:
    SystemUiFont() = default;
    ~SystemUiFont();

    SystemUiFont(const SystemUiFont&) = delete;
    SystemUiFont& operator=(const SystemUiFont&) = delete;

    [[nodiscard]] bool refresh(UINT dpi) noexcept;
    [[nodiscard]] HFONT get() const noexcept;

private:
    HFONT font_{};
};

[[nodiscard]] std::wstring systemUiFontFamily(UINT dpi);
[[nodiscard]] bool isHighContrastEnabled() noexcept;
[[nodiscard]] bool isSystemAppearanceMessage(UINT message) noexcept;
void applySystemUiFont(HWND window, HFONT font) noexcept;

} // namespace hlaunch::ui
