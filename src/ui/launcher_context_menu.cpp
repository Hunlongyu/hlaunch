#include "ui/launcher_context_menu.h"

namespace hlaunch::ui {
namespace {

bool append(const HMENU menu, const UINT flags, const UINT_PTR command,
            const wchar_t* text) noexcept
{
    return AppendMenuW(menu, flags, command, text) != FALSE;
}

} // namespace

wil::unique_hmenu createLauncherContextMenu(const bool pinned)
{
    wil::unique_hmenu menu{CreatePopupMenu()};
    if (!menu
        || !append(menu.get(), MF_STRING | (pinned ? MF_CHECKED : 0U),
                   static_cast<UINT_PTR>(LauncherContextCommand::TogglePin),
                   L"置顶窗口\tCtrl+Space")
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(LauncherContextCommand::Search),
                   L"搜索项目\tCtrl+F")
        || !append(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(LauncherContextCommand::AddPage),
                   L"添加页面...")
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(LauncherContextCommand::Settings),
                   L"设置...\tCtrl+O")
        || !append(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(LauncherContextCommand::Exit),
                   L"退出\tAlt+F4")) {
        return {};
    }
    return menu;
}

wil::unique_hmenu createEmptySlotContextMenu()
{
    wil::unique_hmenu menu{CreatePopupMenu()};
    if (!menu
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(EmptySlotContextCommand::RegisterItem),
                   L"注册项目...")
        || !append(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(EmptySlotContextCommand::InsertSlot),
                   L"插入按钮\tIns")) {
        return {};
    }
    SetMenuDefaultItem(
        menu.get(), static_cast<UINT>(EmptySlotContextCommand::RegisterItem), FALSE);
    return menu;
}

wil::unique_hmenu createTabContextMenu(
    const std::size_t tabIndex,
    const std::size_t tabCount,
    const bool hasItems)
{
    wil::unique_hmenu menu{CreatePopupMenu()};
    wil::unique_hmenu sortMenu{CreatePopupMenu()};
    const UINT unavailable = MF_STRING | MF_DISABLED | MF_GRAYED;
    const bool validTab = tabIndex < tabCount;
    const bool canDelete = validTab && tabCount > 1U;
    const bool canMoveLeft = validTab && tabIndex > 0U;
    const bool canMoveRight = validTab && tabIndex + 1U < tabCount;
    if (!menu || !sortMenu
        || !append(sortMenu.get(), canMoveLeft ? MF_STRING : unavailable,
                   static_cast<UINT_PTR>(TabContextCommand::MovePageLeft),
                   L"向左移动")
        || !append(sortMenu.get(), canMoveRight ? MF_STRING : unavailable,
                   static_cast<UINT_PTR>(TabContextCommand::MovePageRight),
                   L"向右移动")
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(TabContextCommand::AddPage),
                   L"添加页面...")
        || !append(menu.get(), canDelete ? MF_STRING : unavailable,
                   static_cast<UINT_PTR>(TabContextCommand::DeletePage),
                   L"删除页面")
        || !append(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !append(menu.get(), MF_POPUP | MF_STRING,
                   reinterpret_cast<UINT_PTR>(sortMenu.get()),
                   L"排序页面")
        || !append(menu.get(), validTab && hasItems ? MF_STRING : unavailable,
                   static_cast<UINT_PTR>(TabContextCommand::LaunchAll),
                   L"启动页面中的所有项目")
        || !append(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(TabContextCommand::Rename),
                   L"重命名")) {
        return {};
    }
    sortMenu.release();
    return menu;
}

TabDeleteDisposition tabDeleteDisposition(
    const core::ItemsDocument& document,
    const std::size_t tabIndex) noexcept
{
    if (document.tabs.size() <= 1U || tabIndex >= document.tabs.size()) {
        return TabDeleteDisposition::Unavailable;
    }
    return document.tabs[tabIndex].items.empty()
        ? TabDeleteDisposition::Immediate
        : TabDeleteDisposition::ConfirmationRequired;
}

} // namespace hlaunch::ui
