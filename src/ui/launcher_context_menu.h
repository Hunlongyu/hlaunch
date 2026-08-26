#pragma once

#include <Windows.h>
#include <wil/resource.h>

namespace hlaunch::ui {

enum class LauncherContextCommand : UINT_PTR {
    None = 0,
    ToggleLock = 1000,
    Search,
    AddPage,
    Settings,
    Exit,
};

enum class EmptySlotContextCommand : UINT_PTR {
    None = 0,
    RegisterItem = 1100,
    CreateSubmenu,
    InsertSlot,
    DeleteSlot,
};

enum class TabContextCommand : UINT_PTR {
    None = 0,
    AddPage = 1200,
    DeletePage,
    SortPages,
    LaunchAll,
    Properties,
};

[[nodiscard]] wil::unique_hmenu createLauncherContextMenu(bool locked);
[[nodiscard]] wil::unique_hmenu createEmptySlotContextMenu();
[[nodiscard]] wil::unique_hmenu createTabContextMenu(bool canDelete);

} // namespace hlaunch::ui
