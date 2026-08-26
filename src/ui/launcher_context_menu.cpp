#include "ui/launcher_context_menu.h"

namespace hlaunch::ui {
namespace {

bool append(const HMENU menu, const UINT flags, const UINT_PTR command,
            const wchar_t* text) noexcept
{
    return AppendMenuW(menu, flags, command, text) != FALSE;
}

} // namespace

wil::unique_hmenu createLauncherContextMenu(const bool locked)
{
    wil::unique_hmenu menu{CreatePopupMenu()};
    if (!menu
        || !append(menu.get(), MF_STRING | (locked ? MF_CHECKED : 0U),
                   static_cast<UINT_PTR>(LauncherContextCommand::ToggleLock),
                   L"锁定窗口\tCtrl+Space")
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(LauncherContextCommand::Search),
                   L"搜索项目\tCtrl+F")
        || !append(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(LauncherContextCommand::AddPage),
                   L"添加页面...")
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(LauncherContextCommand::Settings),
                   L"选项...\tCtrl+O")
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
    const UINT unavailable = MF_STRING | MF_DISABLED | MF_GRAYED;
    if (!menu
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(EmptySlotContextCommand::RegisterItem),
                   L"注册项目...")
        || !append(menu.get(), unavailable,
                   static_cast<UINT_PTR>(EmptySlotContextCommand::CreateSubmenu),
                   L"创建子菜单")
        || !append(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(EmptySlotContextCommand::InsertSlot),
                   L"插入按钮\tIns")
        || !append(menu.get(), unavailable,
                   static_cast<UINT_PTR>(EmptySlotContextCommand::DeleteSlot),
                   L"删除空按钮\tDel")) {
        return {};
    }
    SetMenuDefaultItem(
        menu.get(), static_cast<UINT>(EmptySlotContextCommand::RegisterItem), FALSE);
    return menu;
}

wil::unique_hmenu createTabContextMenu(const bool canDelete)
{
    wil::unique_hmenu menu{CreatePopupMenu()};
    const UINT unavailable = MF_STRING | MF_DISABLED | MF_GRAYED;
    if (!menu
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(TabContextCommand::AddPage),
                   L"添加页面...")
        || !append(menu.get(), canDelete ? MF_STRING : unavailable,
                   static_cast<UINT_PTR>(TabContextCommand::DeletePage),
                   L"删除页面")
        || !append(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !append(menu.get(), unavailable,
                   static_cast<UINT_PTR>(TabContextCommand::SortPages),
                   L"排序页面")
        || !append(menu.get(), unavailable,
                   static_cast<UINT_PTR>(TabContextCommand::LaunchAll),
                   L"启动页面中的所有项目")
        || !append(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !append(menu.get(), MF_STRING,
                   static_cast<UINT_PTR>(TabContextCommand::Properties),
                   L"属性")) {
        return {};
    }
    return menu;
}

} // namespace hlaunch::ui
