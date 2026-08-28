#pragma once

#include "core/item_operations.h"

#include <windows.h>
#include <wil/resource.h>

#include <cstddef>
#include <optional>

namespace hlaunch::ui {

enum class ItemContextCommand : UINT_PTR {
    None = 0,
    Open = 1,
    Properties = 2,
    Delete = 3,
    Insert = 4,
    RunAsAdministrator = 5,
    OpenLocation = 6,
    CopyName = 7,
    CopyTarget = 8,
    CopyCommandLine = 9,
};

inline constexpr UINT_PTR moveToTabMenuCommandBase = 1'000U;

[[nodiscard]] constexpr UINT_PTR moveToTabMenuCommand(const std::size_t tabIndex) noexcept
{
    return moveToTabMenuCommandBase + tabIndex;
}

[[nodiscard]] std::optional<std::size_t> moveToTabIndexFromMenuCommand(
    UINT_PTR command,
    std::size_t tabCount) noexcept;

[[nodiscard]] wil::unique_hmenu createItemContextMenu(
    const core::ItemsDocument& document,
    core::ItemLocation itemLocation);

} // namespace hlaunch::ui
