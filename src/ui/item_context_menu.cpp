#include "ui/item_context_menu.h"

#include <string>
#include <string_view>

namespace hlaunch::ui {
namespace {

std::wstring utf8ToMenuText(const std::string_view value)
{
    if (value.empty()) {
        return L"（未命名分类）";
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return L"?";
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required) != required) {
        return L"?";
    }
    std::size_t position{};
    while ((position = result.find(L'&', position)) != std::wstring::npos) {
        result.insert(position, 1, L'&');
        position += 2;
    }
    return result;
}

bool appendMenuItem(
    const HMENU menu,
    const UINT flags,
    const UINT_PTR command,
    const wchar_t* label) noexcept
{
    return AppendMenuW(menu, flags, command, label) != FALSE;
}

} // namespace

std::optional<std::size_t> moveToTabIndexFromMenuCommand(
    const UINT_PTR command,
    const std::size_t tabCount) noexcept
{
    if (command < moveToTabMenuCommandBase) {
        return std::nullopt;
    }
    const auto tabIndex = static_cast<std::size_t>(command - moveToTabMenuCommandBase);
    return tabIndex < tabCount ? std::optional<std::size_t>{tabIndex} : std::nullopt;
}

wil::unique_hmenu createItemContextMenu(
    const core::ItemsDocument& document,
    const core::ItemLocation itemLocation)
{
    if (itemLocation.tabIndex >= document.tabs.size()
        || itemLocation.itemIndex >= document.tabs[itemLocation.tabIndex].items.size()) {
        return {};
    }

    wil::unique_hmenu menu{CreatePopupMenu()};
    wil::unique_hmenu copyMenu{CreatePopupMenu()};
    wil::unique_hmenu moveMenu{CreatePopupMenu()};
    if (!menu || !copyMenu || !moveMenu) {
        return {};
    }

    const UINT placeholderFlags = MF_STRING | MF_DISABLED | MF_GRAYED;
    const auto& item = document.tabs[itemLocation.tabIndex].items[itemLocation.itemIndex];
    const UINT openLocationFlags = item.type == core::ItemType::Url
        ? placeholderFlags
        : MF_STRING;
    if (!appendMenuItem(menu.get(), MF_STRING, static_cast<UINT_PTR>(ItemContextCommand::Open), L"打开\tEnter")
        || !appendMenuItem(
            menu.get(),
            MF_STRING,
            static_cast<UINT_PTR>(ItemContextCommand::RunAsAdministrator),
            L"以管理员身份运行")
        || !appendMenuItem(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !appendMenuItem(
            menu.get(),
            openLocationFlags,
            static_cast<UINT_PTR>(ItemContextCommand::OpenLocation),
            L"打开文件所在位置")
        || !appendMenuItem(
            copyMenu.get(), MF_STRING, static_cast<UINT_PTR>(ItemContextCommand::CopyName), L"复制名称")
        || !appendMenuItem(
            copyMenu.get(), MF_STRING, static_cast<UINT_PTR>(ItemContextCommand::CopyTarget), L"复制目标")
        || !appendMenuItem(
            copyMenu.get(),
            MF_STRING,
            static_cast<UINT_PTR>(ItemContextCommand::CopyCommandLine),
            L"复制完整命令")
        || !appendMenuItem(
            menu.get(),
            MF_POPUP | MF_STRING,
            reinterpret_cast<UINT_PTR>(copyMenu.get()),
            L"复制")) {
        return {};
    }
    copyMenu.release();

    if (!appendMenuItem(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !appendMenuItem(menu.get(), MF_STRING, static_cast<UINT_PTR>(ItemContextCommand::Insert), L"插入按钮\tIns")) {
        return {};
    }
    for (std::size_t index = 0; index < document.tabs.size(); ++index) {
        const UINT flags = MF_STRING
            | (index == itemLocation.tabIndex ? MF_DISABLED | MF_GRAYED | MF_CHECKED : 0U);
        const auto label = utf8ToMenuText(document.tabs[index].name);
        if (!appendMenuItem(moveMenu.get(), flags, moveToTabMenuCommand(index), label.c_str())) {
            return {};
        }
    }
    if (!appendMenuItem(
            menu.get(),
            MF_POPUP | MF_STRING,
            reinterpret_cast<UINT_PTR>(moveMenu.get()),
            L"移动到分类")) {
        return {};
    }
    moveMenu.release();

    if (!appendMenuItem(menu.get(), MF_STRING, static_cast<UINT_PTR>(ItemContextCommand::Delete), L"删除\tDel")
        || !appendMenuItem(menu.get(), MF_SEPARATOR, 0, nullptr)
        || !appendMenuItem(menu.get(), MF_STRING, static_cast<UINT_PTR>(ItemContextCommand::Properties), L"属性\tCtrl+P")) {
        return {};
    }
    SetMenuDefaultItem(menu.get(), static_cast<UINT>(ItemContextCommand::Open), FALSE);
    return menu;
}

} // namespace hlaunch::ui
