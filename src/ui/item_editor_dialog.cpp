#include "ui/item_editor_dialog.h"
#include "ui/dpi_layout.h"

#include "ui/native_dialog_template.h"
#include "ui/system_appearance.h"
#include "ui/task_dialog.h"

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

constexpr int editorClientWidth = 620;
constexpr int editorClientHeight = 464;
constexpr int idName = 1001;
constexpr int idType = 1002;
constexpr int idTarget = 1003;
constexpr int idArguments = 1004;
constexpr int idWorkingDirectory = 1005;
constexpr int idIcon = 1006;
constexpr int idTab = 1007;
constexpr int idAdministrator = 1008;
constexpr int idBrowseTarget = 1009;
constexpr int idBrowseWorkingDirectory = 1010;
constexpr int idBrowseIcon = 1011;
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

SIZE windowSizeForClientDip(
    const HWND window,
    const int clientWidthDip,
    const int clientHeightDip,
    const UINT dpi) noexcept
{
    RECT bounds{
        0,
        0,
        scaleDip(clientWidthDip, dpi),
        scaleDip(clientHeightDip, dpi),
    };
    const auto style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    const auto extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    if (!AdjustWindowRectExForDpi(&bounds, style, FALSE, extendedStyle, dpi)) {
        AdjustWindowRectEx(&bounds, style, FALSE, extendedStyle);
    }
    return SIZE{
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
    };
}

enum class BrowseKind {
    File,
    Folder,
    Icon,
};

std::optional<std::wstring> browsePath(const HWND owner, const BrowseKind kind)
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
        options |= kind == BrowseKind::Folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
        dialog->SetOptions(options);
    }
    if (kind == BrowseKind::Icon) {
        constexpr std::array filters{
            COMDLG_FILTERSPEC{L"图标文件", L"*.ico;*.png;*.svg;*.exe;*.dll"},
            COMDLG_FILTERSPEC{L"所有文件", L"*.*"},
        };
        dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
        dialog->SetFileTypeIndex(1);
        dialog->SetDefaultExtension(L"ico");
    }
    switch (kind) {
    case BrowseKind::Folder:
        dialog->SetTitle(L"选择文件夹");
        break;
    case BrowseKind::Icon:
        dialog->SetTitle(L"选择图标文件");
        break;
    case BrowseKind::File:
        dialog->SetTitle(L"选择文件或程序");
        break;
    }
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
        : tabs_(tabs), initialTab_(initialTab), initialItem_(item), addMode_(item == nullptr)
    {}

    static INT_PTR CALLBACK procedure(
        const HWND dialog, const UINT message, const WPARAM wParam, const LPARAM lParam) noexcept
    {
        try {
        auto* self = reinterpret_cast<EditorState*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (message == WM_INITDIALOG) {
            self = reinterpret_cast<EditorState*>(lParam);
            self->dialog_ = dialog;
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->handle(message, wParam, lParam) : FALSE;
        } catch (...) { if (IsWindow(dialog)) EndDialog(dialog, IDCANCEL); return FALSE; }
    }

    [[nodiscard]] std::optional<ItemEditorResult> takeResult()
    {
        return std::move(result_);
    }

private:
    INT_PTR handle(const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        if (message == WM_INITDIALOG) {
            initialize();
            return FALSE;
        }
        if (message == WM_COMMAND) {
            switch (LOWORD(wParam)) {
            case idBrowseTarget:
                browseTarget();
                return TRUE;
            case idBrowseWorkingDirectory:
                browseInto(workingDirectory_, BrowseKind::Folder);
                return TRUE;
            case idBrowseIcon:
                browseInto(icon_, BrowseKind::Icon);
                return TRUE;
            case idType:
                if (HIWORD(wParam) == CBN_SELCHANGE) {
                    typeExplicitlySelected_ = true;
                }
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
        if (message == WM_DPICHANGED) {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam); // NOLINT(performance-no-int-to-ptr): WM_DPICHANGED defines LPARAM as RECT*.
            SetWindowPos(
                dialog_, nullptr,
                suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
            layoutDialogControls(controlLayouts_, GetDpiForWindow(dialog_));
            refreshSystemAppearance();
            return TRUE;
        }
        if (isSystemAppearanceMessage(message)) {
            refreshSystemAppearance();
            return TRUE;
        }
        return FALSE;
    }

    void initialize()
    {
        const UINT dpi = GetDpiForWindow(dialog_);
        const auto windowSize = windowSizeForClientDip(
            dialog_, editorClientWidth, editorClientHeight, dpi);
        const int width = windowSize.cx;
        const int height = windowSize.cy;
        SetWindowTextW(dialog_, addMode_ ? L"添加项目" : L"编辑项目");

        RECT ownerBounds{};
        GetWindowRect(GetParent(dialog_), &ownerBounds);
        const int x = ownerBounds.left
            + std::max<LONG>(0, (ownerBounds.right - ownerBounds.left - width) / 2);
        const int y = ownerBounds.top
            + std::max<LONG>(0, (ownerBounds.bottom - ownerBounds.top - height) / 2);
        SetWindowPos(dialog_, nullptr, x, y, width, height,
                     SWP_NOACTIVATE | SWP_NOZORDER);

        static_cast<void>(systemUiFont_.refresh(GetDpiForWindow(dialog_)));

        createControls();
        populate();
        refreshSystemAppearance();
        SetFocus(addMode_ ? target_ : name_);
    }

    void refreshSystemAppearance()
    {
        static_cast<void>(systemUiFont_.refresh(GetDpiForWindow(dialog_)));
        applySystemUiFont(dialog_, systemUiFont_.get());
        applyNativeWindowStyle(dialog_, false);
        EnumChildWindows(
            dialog_,
            [](const HWND child, const LPARAM) noexcept -> BOOL {
                applyNativeControlStyle(child, false);
                return TRUE;
            },
            0);
        RedrawWindow(
            dialog_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }

    HWND add(
        const wchar_t* cls,
        const wchar_t* text,
        const DWORD style,
        const int x,
        const int y,
        const int width,
        const int height,
        const int id)
    {
        const UINT dpi = GetDpiForWindow(dialog_);
        const auto control = CreateWindowExW(
            0, cls, text, WS_CHILD | WS_VISIBLE | style,
            scaleDip(x, dpi), scaleDip(y, dpi), scaleDip(width, dpi), scaleDip(height, dpi), dialog_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(
            control, WM_SETFONT,
            reinterpret_cast<WPARAM>(systemUiFont_.get()), TRUE);
        controlLayouts_.push_back({control, x, y, width, height});
        return control;
    }

    void createControls()
    {
        auto label = [&](const wchar_t* text, const int y) {
            add(L"STATIC", text, 0, 20, y + 2, 112, 20, 0);
        };
        constexpr int fieldX = 140;
        constexpr int fieldWidth = 460;
        constexpr int pathWidth = 354;
        constexpr int browseX = 502;
        constexpr int browseWidth = 98;
        constexpr int fieldHeight = 23;
        label(L"名称：", 20);
        name_ = add(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                    fieldX, 20, fieldWidth, fieldHeight, idName);
        label(L"类型：", 53);
        type_ = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                    fieldX, 51, 240, 180, idType);
        for (const wchar_t* value : {L"应用", L"文件", L"文件夹", L"网址", L"快捷方式"}) {
            SendMessageW(type_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        }
        SendMessageW(type_, CB_SETCURSEL, 0, 0);
        label(L"目标：", 86);
        target_ = add(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                      fieldX, 84, pathWidth, fieldHeight, idTarget);
        add(L"BUTTON", L"浏览...", WS_TABSTOP,
            browseX, 82, browseWidth, 27, idBrowseTarget);
        label(L"参数（每行一个）：", 119);
        arguments_ = add(
            L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_MULTILINE
                | ES_AUTOVSCROLL | WS_VSCROLL,
            fieldX, 117, fieldWidth, 80, idArguments);
        label(L"工作目录：", 211);
        workingDirectory_ = add(
            L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            fieldX, 209, pathWidth, fieldHeight, idWorkingDirectory);
        add(L"BUTTON", L"浏览...", WS_TABSTOP,
            browseX, 207, browseWidth, 27, idBrowseWorkingDirectory);
        add(
            L"STATIC", L"程序运行时使用的起始文件夹；通常留空即可。",
            0, fieldX, 238, fieldWidth, 18, 0);
        label(L"图标：", 268);
        icon_ = add(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                    fieldX, 266, pathWidth, fieldHeight, idIcon);
        add(L"BUTTON", L"浏览...", WS_TABSTOP,
            browseX, 264, browseWidth, 27, idBrowseIcon);
        label(L"所属页面：", 301);
        tab_ = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                   fieldX, 299, 240, 180, idTab);
        for (const auto& tab : tabs_) {
            const auto name = utf8ToWide(tab.name);
            SendMessageW(tab_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        administrator_ = add(
            L"BUTTON", L"以管理员身份运行", BS_AUTOCHECKBOX | WS_TABSTOP,
            fieldX, 340, 220, 25, idAdministrator);
        add(
            L"STATIC", L"名称留空时自动使用目标名称；图标留空时使用目标的系统图标。",
            0, fieldX, 376, fieldWidth, 24, 0);
        add(L"BUTTON", addMode_ ? L"添加" : L"保存", WS_TABSTOP | BS_DEFPUSHBUTTON,
            420, 416, 80, 28, IDOK);
        add(L"BUTTON", L"取消", WS_TABSTOP,
            510, 416, 80, 28, IDCANCEL);

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

    void browseTarget()
    {
        const auto selection = SendMessageW(type_, CB_GETCURSEL, 0, 0);
        const auto kind = selection == static_cast<LRESULT>(core::ItemType::Folder)
            ? BrowseKind::Folder
            : BrowseKind::File;
        if (const auto selected = browsePath(dialog_, kind)) {
            SetWindowTextW(target_, selected->c_str());
            const auto inferredType = inferType(*selected);
            SendMessageW(type_, CB_SETCURSEL, static_cast<WPARAM>(inferredType), 0);
            typeExplicitlySelected_ = false;
            if (addMode_ && controlText(name_).empty()) {
                const auto name = suggestedName(*selected);
                SetWindowTextW(name_, name.c_str());
            }
        }
    }

    void browseInto(const HWND control, const BrowseKind kind)
    {
        if (const auto selected = browsePath(dialog_, kind)) {
            SetWindowTextW(control, selected->c_str());
        }
    }

    void save()
    {
        std::wstring nameValue = controlText(name_);
        const std::wstring targetValue = controlText(target_);
        if (addMode_ && nameValue.empty()) {
            nameValue = suggestedName(targetValue);
        }
        const auto name = wideToUtf8(nameValue);
        const auto target = wideToUtf8(targetValue);
        if (!name || name->empty() || !target || target->empty()) {
            showTaskMessage(
                dialog_, L"HLaunch 项目", L"名称和目标不能为空。",
                TaskDialogIcon::Warning);
            return;
        }
        if (name->size() > maximumItemNameBytes || target->size() > maximumFieldBytes) {
            showTaskMessage(
                dialog_, L"HLaunch 项目", L"名称或目标过长。",
                TaskDialogIcon::Warning);
            return;
        }

        core::LaunchItem item{};
        item.name = *name;
        item.target = *target;
        item.type = addMode_ && !typeExplicitlySelected_
            ? inferType(targetValue)
            : static_cast<core::ItemType>(std::clamp<LRESULT>(
                  SendMessageW(type_, CB_GETCURSEL, 0, 0), 0, 4));

        item.runAsAdministrator =
            SendMessageW(administrator_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const auto working = wideToUtf8(controlText(workingDirectory_));
        const auto icon = wideToUtf8(controlText(icon_));
        if (!working || !icon
            || working->size() > maximumFieldBytes
            || icon->size() > maximumFieldBytes) {
            showTaskMessage(
                dialog_, L"HLaunch 项目", L"工作目录或图标路径无效。",
                TaskDialogIcon::Warning);
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
                    showTaskMessage(
                        dialog_, L"HLaunch 项目", L"参数内容无效或数量过多。",
                        TaskDialogIcon::Warning);
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
        const auto selectedTab = static_cast<std::size_t>(tabSelection);

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
    bool addMode_{};
    bool typeExplicitlySelected_{};
    SystemUiFont systemUiFont_{};
    std::optional<ItemEditorResult> result_{};
    std::vector<DialogControlLayout> controlLayouts_{};
};

} // namespace

std::optional<ItemEditorResult> ItemEditorDialog::show(
    const HWND owner,
    const std::vector<core::Tab>& tabs,
    const std::size_t initialTabIndex,
    const core::LaunchItem* initialItem)
{
    if (tabs.empty()) {
        showTaskMessage(
            owner, L"HLaunch 项目", L"请先创建页面。",
            TaskDialogIcon::Information);
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
