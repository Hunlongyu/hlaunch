#pragma once

#include "core/item_operations.h"

#include <windows.h>
#include <wil/resource.h>

namespace hlaunch::ui {

enum class ItemContextCommand : UINT_PTR {
    None = 0,
    Open = 1,
    Edit = 2,
    Delete = 3,
};

[[nodiscard]] wil::unique_hmenu createItemContextMenu(
    const core::ItemsDocument& document,
    core::ItemLocation itemLocation);

} // namespace hlaunch::ui
