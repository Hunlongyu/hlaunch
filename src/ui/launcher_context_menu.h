#pragma once

#include "core/data_model.h"

#include <Windows.h>
#include <wil/resource.h>

#include <cstddef>

namespace hlaunch::ui {

enum class LauncherContextCommand : UINT_PTR {
    None = 0,
    TogglePin = 1000,
    Search,
    AddPage,
    Settings,
    Exit,
};

enum class EmptySlotContextCommand : UINT_PTR {
    None = 0,
    RegisterItem = 1100,
    InsertSlot,
};

enum class TabContextCommand : UINT_PTR {
    None = 0,
    AddPage = 1200,
    DeletePage,
    MovePageLeft,
    MovePageRight,
    LaunchAll,
    Rename,
};

enum class TabDeleteDisposition {
    Unavailable,
    Immediate,
    ConfirmationRequired,
};

[[nodiscard]] wil::unique_hmenu createLauncherContextMenu(bool pinned);
[[nodiscard]] wil::unique_hmenu createEmptySlotContextMenu();
[[nodiscard]] wil::unique_hmenu createTabContextMenu(
    std::size_t tabIndex,
    std::size_t tabCount,
    bool hasItems = false);
[[nodiscard]] TabDeleteDisposition tabDeleteDisposition(
    const core::ItemsDocument& document,
    std::size_t tabIndex) noexcept;

} // namespace hlaunch::ui
