#pragma once

#include "core/data_model.h"
#include "ui/theme.h"

#include <Windows.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace hlaunch::ui {

struct ItemEditorResult
{
    core::LaunchItem item{};
    std::size_t tabIndex{};
};

class ItemEditorDialog final
{
  public:
    [[nodiscard]] static std::optional<ItemEditorResult>
    show(HWND owner, const std::vector<core::Tab> &tabs, std::size_t initialTabIndex,
         const core::LaunchItem *initialItem = nullptr,
         core::ThemeMode themeMode = core::ThemeMode::Dark);
};

} // namespace hlaunch::ui
