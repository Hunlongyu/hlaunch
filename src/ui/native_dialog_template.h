#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstring>

namespace hlaunch::ui {

class NativeDialogTemplate final {
public:
    NativeDialogTemplate()
    {
        DLGTEMPLATE dialog{};
        dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
        dialog.dwExtendedStyle = WS_EX_CONTROLPARENT;
        dialog.cdit = 0;
        dialog.x = 0;
        dialog.y = 0;
        dialog.cx = 100;
        dialog.cy = 100;
        std::memcpy(storage_.data(), &dialog, sizeof(dialog));

        auto* cursor = reinterpret_cast<WORD*>(storage_.data() + sizeof(dialog));
        *cursor++ = 0; // no menu
        *cursor++ = 0; // default dialog class
        *cursor = 0;   // title is assigned during WM_INITDIALOG
    }

    [[nodiscard]] const DLGTEMPLATE* get() const noexcept
    {
        return reinterpret_cast<const DLGTEMPLATE*>(storage_.data());
    }

private:
    alignas(DWORD) std::array<std::byte, 64> storage_{};
};

} // namespace hlaunch::ui
