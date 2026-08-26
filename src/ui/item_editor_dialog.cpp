#include "ui/item_editor_dialog.h"

#include "ui/native_dialog_template.h"

#include <Windows.h>
#include <ShObjIdl.h>
#include <Shlwapi.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>

namespace hlaunch::ui {
namespace {

constexpr int quickWidth = 510;
constexpr int quickHeight = 270;
constexpr int propertiesWidth = 560;
constexpr int propertiesHeight = 470;
constexpr int idName = 1001;
constexpr int idType = 1002;
constexpr int idTarget = 1003;
constexpr int idArguments = 1004;
constexpr int idWorkingDirectory = 1005;
constexpr int idIcon = 1006;
constexpr int idTab = 1007;
constexpr int idAdministrator = 1008;
constexpr int idBrowseFile = 1009;
constexpr int idBrowseFolder = 1010;
constexpr std::size_t maximumFieldBytes = 32'768;
constexpr std::size_t maximumItemNameBytes = 256;
constexpr std::size_t maximumArgumentCount = 256;

std::wstring utf8ToWide(const std::string_view value)
{
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), size);
    return result;
}

std::optional<std::string> wideToUtf8(const std::wstring_view value)
{
    if (value.empty()) {
        return std::string{};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::nullopt;
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), size, nullptr, nullptr) != size) {
        return std::nullopt;
    }
    return result;
}

std::wstring controlText(const HWND control)
{
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
    const int copied = GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    value.resize(static_cast<std::size_t>(std::max(0, copied)));
    return value;
}

std::optional<std::wstring> browsePath(const HWND owner, const bool folder)
{
    winrt::com_ptr<IFileOpenDialog> dialog{};
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(dialog.put())))) {
        return std::nullopt;
    }
    FILEOPENDIALOGOPTIONS options{};
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        options |= folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
        dialog->SetOptions(options);
    }
    dialog->SetTitle(folder ? L"选择文件夹" : L"选择文件或程序");
    if (dialog->Show(owner) != S_OK) {
        return std::nullopt;
    }
    winrt::com_ptr<IShellItem> item{};
    if (FAILED(dialog->GetResult(item.put()))) {
        return std::nullopt;
    }
    PWSTR rawPath{};
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || !rawPath) {
        return std::nullopt;
    }
    std::wstring result{rawPath};
    CoTaskMemFree(rawPath);
    return result;
}

core::ItemType inferType(const std::wstring& target)
{
    if (PathIsURLW(target.c_str())) {
        return core::ItemType::Url;
    }
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return core::ItemType::Folder;
    }
    const auto* extension = PathFindExtensionW(target.c_str());
    if (extension && _wcsicmp(extension, L".lnk") == 0) {
        return core::ItemType::Shortcut;
    }
    if (extension
        && (_wcsicmp(extension, L".exe") == 0
            || _wcsicmp(extension, L".com") == 0
            || _wcsicmp(extension, L".bat") == 0
            || _wcsicmp(extension, L".cmd") == 0)) {
        return core::ItemType::Application;
    }
    return core::ItemType::File;
}

std::wstring suggestedName(const std::wstring& target)
{
    if (PathIsURLW(target.c_str())) {
        return target;
    }
    const auto* fileName = PathFindFileNameW(target.c_str());
    std::wstring result = fileName && *fileName ? fileName : target;
    if (!result.empty()) {
        std::vector<wchar_t> buffer(result.begin(), result.end());
        buffer.push_back(L'\0');
        PathRemoveExtensionW(buffer.data());
        if (*buffer.data()) {
            result = buffer.data();
        }
    }
    return result;
}

class EditorState final {
public:
    EditorState(
        const std::vector<core::Tab>& tabs,
        const std::size_t initialTab,
        const core::LaunchItem* item)
        : tabs_(tabs), initialTab_(initialTab), initialItem_(item), quickMode_(item == nullptr)
    {}

    static INT_PTR CALLBACK procedure(
        const HWND dialog, const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        auto* self = reinterpret_cast<EditorState*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (message == WM_INITDIALOG) {
            self = reinterpret_cast<EditorState*>(lParam);
            self->dialog_ = dialog;
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->handle(message, wParam) : FALSE;
    }

    [[nodiscard]] std::optional<ItemEditorResult> takeResult()
    {
        return std::move(result_);
    }

private:
    INT_PTR handle(const UINT message, const WPARAM wParam)
    {
        if (message == WM_INITDIALOG) {
            initialize();
            return FALSE;
        }
        if (message == WM_COMMAND) {
            switch (LOWORD(wParam)) {
            case idBrowseFile:
                browse(false);
                return TRUE;
            case idBrowseFolder:
                browse(true);
                return TRUE;
            case IDOK:
                save();
                return TRUE;
            case IDCANCEL:
                EndDialog(dialog_, IDCANCEL);
                return TRUE;
            default:
                return FALSE;
            }
        }
        if (message == WM_CLOSE) {
            EndDialog(dialog_, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }

    void initialize()
    {
        const int width = quickMode_ ? quickWidth : propertiesWidth;
        const int height = quickMode_ ? quickHeight : propertiesHeight;
        SetWindowTextW(dialog_, quickMode_ ? L"注册项目" : L"项目属性");

        RECT ownerBounds{};
        GetWindowRect(GetParent(dialog_), &ownerBounds);
        const int x = ownerBounds.left
            + std::max<LONG>(0, (ownerBounds.right - ownerBounds.left - width) / 2);
        const int y = ownerBounds.top
            + std::max<LONG>(0, (ownerBounds.bottom - ownerBounds.top - height) / 2);
        SetWindowPos(dialog_, nullptr, x, y, width, height,
                     SWP_NOACTIVATE | SWP_NOZORDER);

        if (quickMode_) {
            createQuickControls();
        }
        else {
            createPropertyControls();
            populate();
        }
        SetFocus(quickMode_ ? target_ : name_);
    }

    HWND add(
        const wchar_t* cls,
        const wchar_t* text,
        const DWORD style,
        const int x,
        const int y,
        const int width,
        const int height,
        const int id) const
    {
        const auto control = CreateWindowExW(
            0, cls, text, WS_CHILD | WS_VISIBLE | style,
            x, y, width, height, dialog_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(
            control, WM_SETFONT,
            reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return control;
    }

    void createQuickControls()
    {
        add(L"STATIC", L"文件、文件夹或网址：", 0, 16, 18, 210, 20, 0);
        target_ = add(
            L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            16, 43, 350, 25, idTarget);
        add(L"BUTTON", L"浏览文件...", WS_TABSTOP, 376, 42, 104, 27, idBrowseFile);
        add(L"BUTTON", L"浏览文件夹...", WS_TABSTOP, 376, 76, 104, 27, idBrowseFolder);

        add(L"STATIC", L"名称（可选）：", 0, 16, 88, 120, 20, 0);
        name_ = add(
            L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            124, 84, 242, 25, idName);
        add(L"STATIC", L"留空时自动使用文件名。拖入 Launcher 可跳过此窗口。",
            0, 16, 122, 450, 34, 0);

        add(L"BUTTON", L"添加", WS_TABSTOP | BS_DEFPUSHBUTTON,
            300, 176, 80, 28, IDOK);
        add(L"BUTTON", L"取消", WS_TABSTOP,
            390, 176, 80, 28, IDCANCEL);
        SendMessageW(target_, EM_SETLIMITTEXT, 32'768, 0);
        SendMessageW(name_, EM_SETLIMITTEXT, 256, 0);
    }

    void createPropertyControls()
    {
        auto label = [&](const wchar_t* text, const int y) {
            add(L"STATIC", text, 0, 16, y + 3, 116, 20, 0);
        };
        constexpr int fieldX = 132;
        constexpr int fieldWidth = 392;
        label(L"名称：", 16);
        name_ = add(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                    fieldX, 16, fieldWidth, 25, idName);
        label(L"类型：", 51);
        type_ = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                    fieldX, 49, 190, 180, idType);
        for (const wchar_t* value : {L"应用", L"文件", L"文件夹", L"网址", L"快捷方式"}) {
            SendMessageW(type_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        }
        label(L"目标：", 86);
        target_ = add(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                      fieldX, 84, 286, 25, idTarget);
        add(L"BUTTON", L"浏览...", WS_TABSTOP, 426, 83, 98, 27, idBrowseFile);
        label(L"参数（每行一个）：", 121);
        arguments_ = add(
            L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_MULTILINE
                | ES_AUTOVSCROLL | WS_VSCROLL,
            fieldX, 119, fieldWidth, 82, idArguments);
        label(L"工作目录：", 215);
        workingDirectory_ = add(
            L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            fieldX, 213, fieldWidth, 25, idWorkingDirectory);
        label(L"图标路径：", 250);
        icon_ = add(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                    fieldX, 248, fieldWidth, 25, idIcon);
        label(L"所属页面：", 285);
        tab_ = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                   fieldX, 283, 190, 180, idTab);
        for (const auto& tab : tabs_) {
            const auto name = utf8ToWide(tab.name);
            SendMessageW(tab_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        administrator_ = add(
            L"BUTTON", L"以管理员身份运行", BS_AUTOCHECKBOX | WS_TABSTOP,
            fieldX, 326, 220, 25, idAdministrator);
        add(L"BUTTON", L"确定", WS_TABSTOP | BS_DEFPUSHBUTTON,
            344, 390, 80, 28, IDOK);
        add(L"BUTTON", L"取消", WS_TABSTOP,
            434, 390, 80, 28, IDCANCEL);

        SendMessageW(name_, EM_SETLIMITTEXT, 256, 0);
        SendMessageW(target_, EM_SETLIMITTEXT, 32'768, 0);
        SendMessageW(workingDirectory_, EM_SETLIMITTEXT, 32'768, 0);
        SendMessageW(icon_, EM_SETLIMITTEXT, 32'768, 0);
        SendMessageW(tab_, CB_SETCURSEL,
                     std::min(initialTab_, tabs_.size() - 1U), 0);
    }

    void populate()
    {
        if (!initialItem_) {
            return;
        }
        SetWindowTextW(name_, utf8ToWide(initialItem_->name).c_str());
        SetWindowTextW(target_, utf8ToWide(initialItem_->target).c_str());
        std::wstring arguments{};
        for (const auto& argument : initialItem_->arguments) {
            if (!arguments.empty()) {
                arguments += L"\r\n";
            }
            arguments += utf8ToWide(argument);
        }
        SetWindowTextW(arguments_, arguments.c_str());
        SetWindowTextW(
            workingDirectory_,
            initialItem_->workingDirectory
                ? utf8ToWide(*initialItem_->workingDirectory).c_str()
                : L"");
        SetWindowTextW(
            icon_, initialItem_->icon ? utf8ToWide(*initialItem_->icon).c_str() : L"");
        SendMessageW(type_, CB_SETCURSEL, static_cast<WPARAM>(initialItem_->type), 0);
        SendMessageW(administrator_, BM_SETCHECK,
                     initialItem_->runAsAdministrator ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    void browse(const bool folder)
    {
        if (const auto selected = browsePath(dialog_, folder)) {
            SetWindowTextW(target_, selected->c_str());
            if (quickMode_ && controlText(name_).empty()) {
                const auto name = suggestedName(*selected);
                SetWindowTextW(name_, name.c_str());
            }
        }
    }

    void save()
    {
        std::wstring nameValue = controlText(name_);
        const std::wstring targetValue = controlText(target_);
        if (quickMode_ && nameValue.empty()) {
            nameValue = suggestedName(targetValue);
        }
        const auto name = wideToUtf8(nameValue);
        const auto target = wideToUtf8(targetValue);
        if (!name || name->empty() || !target || target->empty()) {
            MessageBoxW(dialog_, L"名称和目标不能为空。", L"HLaunch 项目",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        if (name->size() > maximumItemNameBytes || target->size() > maximumFieldBytes) {
            MessageBoxW(dialog_, L"名称或目标过长。", L"HLaunch 项目",
                        MB_OK | MB_ICONWARNING);
            return;
        }

        core::LaunchItem item{};
        item.name = *name;
        item.target = *target;
        item.type = quickMode_
            ? inferType(targetValue)
            : static_cast<core::ItemType>(std::clamp<LRESULT>(
                  SendMessageW(type_, CB_GETCURSEL, 0, 0), 0, 4));

        std::size_t selectedTab = initialTab_;
        if (!quickMode_) {
            item.runAsAdministrator =
                SendMessageW(administrator_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            const auto working = wideToUtf8(controlText(workingDirectory_));
            const auto icon = wideToUtf8(controlText(icon_));
            if (!working || !icon
                || working->size() > maximumFieldBytes
                || icon->size() > maximumFieldBytes) {
                MessageBoxW(dialog_, L"工作目录或图标路径无效。", L"HLaunch 项目",
                            MB_OK | MB_ICONWARNING);
                return;
            }
            if (!working->empty()) {
                item.workingDirectory = *working;
            }
            if (!icon->empty()) {
                item.icon = *icon;
            }

            std::wstring lines = controlText(arguments_);
            std::size_t start{};
            while (start <= lines.size()) {
                const auto end = lines.find_first_of(L"\r\n", start);
                const auto line = lines.substr(
                    start, end == std::wstring::npos ? lines.size() - start : end - start);
                if (!line.empty()) {
                    const auto value = wideToUtf8(line);
                    if (!value || value->size() > maximumFieldBytes
                        || item.arguments.size() >= maximumArgumentCount) {
                        MessageBoxW(dialog_, L"参数内容无效或数量过多。", L"HLaunch 项目",
                                    MB_OK | MB_ICONWARNING);
                        return;
                    }
                    item.arguments.push_back(*value);
                }
                if (end == std::wstring::npos) {
                    break;
                }
                start = end + 1U;
                if (start < lines.size() && lines[end] == L'\r' && lines[start] == L'\n') {
                    ++start;
                }
            }
            const auto tabSelection = SendMessageW(tab_, CB_GETCURSEL, 0, 0);
            if (tabSelection == CB_ERR) {
                return;
            }
            selectedTab = static_cast<std::size_t>(tabSelection);
        }

        result_ = ItemEditorResult{std::move(item), selectedTab};
        EndDialog(dialog_, IDOK);
    }

    HWND dialog_{};
    HWND name_{};
    HWND type_{};
    HWND target_{};
    HWND arguments_{};
    HWND workingDirectory_{};
    HWND icon_{};
    HWND tab_{};
    HWND administrator_{};
    const std::vector<core::Tab>& tabs_;
    std::size_t initialTab_{};
    const core::LaunchItem* initialItem_{};
    bool quickMode_{};
    std::optional<ItemEditorResult> result_{};
};

} // namespace

std::optional<ItemEditorResult> ItemEditorDialog::show(
    const HWND owner,
    const std::vector<core::Tab>& tabs,
    const std::size_t initialTabIndex,
    const core::LaunchItem* initialItem,
    const core::ThemeMode themeMode)
{
    (void)themeMode;
    if (tabs.empty()) {
        MessageBoxW(owner, L"请先创建页面。", L"HLaunch 项目",
                    MB_OK | MB_ICONINFORMATION);
        return std::nullopt;
    }
    EditorState state{tabs, initialTabIndex, initialItem};
    const NativeDialogTemplate dialogTemplate{};
    const auto result = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr), dialogTemplate.get(), owner,
        EditorState::procedure, reinterpret_cast<LPARAM>(&state));
    return result == IDOK ? state.takeResult() : std::nullopt;
}

} // namespace hlaunch::ui
