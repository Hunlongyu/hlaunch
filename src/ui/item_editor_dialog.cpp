#include "ui/item_editor_dialog.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace hlaunch::ui {
namespace {

constexpr wchar_t editorClass[] = L"HLaunch.ItemEditor.v1";
constexpr int editorWidth = 560;
constexpr int editorHeight = 610;
constexpr int idName = 1001;
constexpr int idType = 1002;
constexpr int idTarget = 1003;
constexpr int idArguments = 1004;
constexpr int idWorkingDirectory = 1005;
constexpr int idIcon = 1006;
constexpr int idTab = 1007;
constexpr int idAdministrator = 1008;
constexpr int idSave = 1009;
constexpr int idCancel = 1010;
constexpr std::size_t maximumItemNameBytes = 256;
constexpr std::size_t maximumFieldBytes = 32'768;
constexpr std::size_t maximumArgumentCount = 256;

std::wstring utf8ToWide(const std::string_view value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                               static_cast<int>(value.size()), result.data(), size) == size
               ? result
               : std::wstring{};
}

std::optional<std::string> wideToUtf8(const std::wstring_view value)
{
    if (value.empty())
        return std::string{};
    const int size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return std::nullopt;
    std::string result(static_cast<std::size_t>(size), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                               static_cast<int>(value.size()), result.data(), size, nullptr,
                               nullptr) == size
               ? std::optional{result}
               : std::nullopt;
}

std::wstring controlText(const HWND control)
{
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
    const int copied = GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    value.resize(static_cast<std::size_t>(std::max(0, copied)));
    return value;
}

class EditorState final
{
  public:
    EditorState(const std::vector<core::Tab> &tabs, const std::size_t initialTab,
                const core::LaunchItem *item)
        : tabs_(tabs), initialTab_(initialTab), initialItem_(item)
    {}

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        EditorState *self{};
        if (message == WM_NCCREATE)
        {
            const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam); // NOLINT(performance-no-int-to-ptr): Win32 LPARAM carries CREATESTRUCTW*.
            self = static_cast<EditorState *>(create->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<EditorState *>(GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr): Win32 stores this pointer as LONG_PTR.
        }
        return self ? self->handle(message, wParam, lParam)
                    : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT handle(const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        if (message == WM_CREATE)
        {
            createControls();
            return 0;
        }
        if (message == WM_COMMAND)
        {
            if (LOWORD(wParam) == idSave)
            {
                save();
                return 0;
            }
            if (LOWORD(wParam) == idCancel)
            {
                finish();
                return 0;
            }
        }
        if (message == WM_CLOSE)
        {
            finish();
            return 0;
        }
    if (message == WM_DESTROY)
    {
        window_ = nullptr;
        return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    void createControls()
    {
        const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto add = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y,
                       int width, int height, int id) {
            const auto control =
                CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
                                window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), // NOLINT(performance-no-int-to-ptr): Child controls encode their integer ID in HMENU.
                                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return control;
        };
        auto label = [&](const wchar_t *text, int y) {
            add(L"STATIC", text, 0, 20, y, 120, 20, 0);
        };
        const int fieldX = 145;
        label(L"名称", 22);
        name_ =
            add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, fieldX, 18, 380, 25, idName);
        label(L"类型", 60);
        type_ = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, fieldX, 56, 220, 200, idType);
        for (const wchar_t *value : {L"应用", L"文件", L"文件夹", L"网址", L"快捷方式"})
            SendMessageW(type_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        label(L"目标", 98);
        target_ = add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, fieldX, 94, 380, 25,
                      idTarget);
        label(L"参数（每行一个）", 136);
        arguments_ =
            add(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
                fieldX, 132, 380, 100, idArguments);
        label(L"工作目录（可选）", 250);
        workingDirectory_ = add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, fieldX, 246,
                                380, 25, idWorkingDirectory);
        label(L"图标路径（可选）", 288);
        icon_ = add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, fieldX, 284, 380, 25,
                    idIcon);
        label(L"所属分类", 326);
        tab_ = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, fieldX, 322, 220, 200, idTab);
        for (const auto &tab : tabs_)
        {
            const auto name = utf8ToWide(tab.name);
            SendMessageW(tab_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        administrator_ = add(L"BUTTON", L"以管理员身份运行", BS_AUTOCHECKBOX | WS_TABSTOP, fieldX,
                             366, 220, 25, idAdministrator);
        add(L"BUTTON", L"保存", BS_DEFPUSHBUTTON | WS_TABSTOP, 325, 525, 95, 32, idSave);
        add(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, 430, 525, 95, 32, idCancel);
        SendMessageW(tab_, CB_SETCURSEL,
                     std::min(initialTab_, tabs_.empty() ? 0U : tabs_.size() - 1U), 0);
        SendMessageW(type_, CB_SETCURSEL, 0, 0);
        SendMessageW(name_, EM_SETLIMITTEXT, 256, 0);
        SendMessageW(target_, EM_SETLIMITTEXT, 32'768, 0);
        SendMessageW(workingDirectory_, EM_SETLIMITTEXT, 32'768, 0);
        SendMessageW(icon_, EM_SETLIMITTEXT, 32'768, 0);
        if (initialItem_)
            populate();
        SetFocus(name_);
    }

    void populate()
    {
        SetWindowTextW(name_, utf8ToWide(initialItem_->name).c_str());
        SetWindowTextW(target_, utf8ToWide(initialItem_->target).c_str());
        std::wstring args;
        for (const auto &arg : initialItem_->arguments)
        {
            if (!args.empty())
                args += L"\r\n";
            args += utf8ToWide(arg);
        }
        SetWindowTextW(arguments_, args.c_str());
        SetWindowTextW(workingDirectory_, initialItem_->workingDirectory
                                              ? utf8ToWide(*initialItem_->workingDirectory).c_str()
                                              : L"");
        SetWindowTextW(icon_, initialItem_->icon ? utf8ToWide(*initialItem_->icon).c_str() : L"");
        SendMessageW(type_, CB_SETCURSEL, static_cast<WPARAM>(initialItem_->type), 0);
        SendMessageW(administrator_, BM_SETCHECK,
                     initialItem_->runAsAdministrator ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    void save()
    {
        const auto name = wideToUtf8(controlText(name_));
        const auto target = wideToUtf8(controlText(target_));
        if (!name || name->empty() || !target || target->empty())
        {
            MessageBoxW(window_, L"名称和目标不能为空。", L"HLaunch 条目", MB_OK | MB_ICONWARNING);
            return;
        }
        if (name->size() > maximumItemNameBytes || target->size() > maximumFieldBytes)
        {
            MessageBoxW(window_, L"名称或目标过长，请缩短后重试。", L"HLaunch 条目",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        core::LaunchItem item{};
        item.name = *name;
        item.target = *target;
        item.type = static_cast<core::ItemType>(
            std::clamp<LRESULT>(SendMessageW(type_, CB_GETCURSEL, 0, 0), 0, 4));
        item.runAsAdministrator = SendMessageW(administrator_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const auto working = wideToUtf8(controlText(workingDirectory_));
        const auto icon = wideToUtf8(controlText(icon_));
        if (!working || !icon)
        {
            MessageBoxW(window_, L"文本包含无效字符。", L"HLaunch 条目", MB_OK | MB_ICONWARNING);
            return;
        }
        if (working->size() > maximumFieldBytes || icon->size() > maximumFieldBytes)
        {
            MessageBoxW(window_, L"工作目录或图标路径过长。", L"HLaunch 条目",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        if (!working->empty())
            item.workingDirectory = *working;
        if (!icon->empty())
            item.icon = *icon;
        std::wstring lines = controlText(arguments_);
        std::size_t start{};
        while (start <= lines.size())
        {
            const auto end = lines.find_first_of(L"\r\n", start);
            const auto line =
                lines.substr(start, end == std::wstring::npos ? lines.size() - start : end - start);
            if (!line.empty())
            {
                const auto value = wideToUtf8(line);
                if (!value || value->size() > maximumFieldBytes)
                {
                    MessageBoxW(window_, L"参数包含无效字符或单项过长。", L"HLaunch 条目",
                                MB_OK | MB_ICONWARNING);
                    return;
                }
                item.arguments.push_back(*value);
                if (item.arguments.size() > maximumArgumentCount)
                {
                    MessageBoxW(window_, L"参数不能超过 256 项。", L"HLaunch 条目",
                                MB_OK | MB_ICONWARNING);
                    return;
                }
            }
            if (end == std::wstring::npos)
                break;
            start = end + 1U;
            if (start < lines.size() && lines[end] == L'\r' && lines[start] == L'\n')
                ++start;
        }
        const auto selectedTab = SendMessageW(tab_, CB_GETCURSEL, 0, 0);
        if (selectedTab == CB_ERR)
            return;
        result_ = ItemEditorResult{std::move(item), static_cast<std::size_t>(selectedTab)};
        finish();
    }

    void finish()
    {
        if (window_)
            DestroyWindow(window_);
    }

    std::optional<ItemEditorResult> takeResult() { return std::move(result_); }

    HWND window_{};
    HWND name_{};
    HWND type_{};
    HWND target_{};
    HWND arguments_{};
    HWND workingDirectory_{};
    HWND icon_{};
    HWND tab_{};
    HWND administrator_{};
    const std::vector<core::Tab> &tabs_;
    std::size_t initialTab_{};
    const core::LaunchItem *initialItem_{};
    std::optional<ItemEditorResult> result_{};
};

} // namespace

std::optional<ItemEditorResult> ItemEditorDialog::show(HWND owner,
                                                       const std::vector<core::Tab> &tabs,
                                                       std::size_t initialTabIndex,
                                                       const core::LaunchItem *initialItem)
{
    if (tabs.empty())
    {
        MessageBoxW(owner, L"请先创建分类。", L"HLaunch 条目", MB_OK | MB_ICONINFORMATION);
        return std::nullopt;
    }
    WNDCLASSEXW cls{sizeof(WNDCLASSEXW)};
    cls.lpfnWndProc = &EditorState::procedure;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); // NOLINT(performance-no-int-to-ptr): Win32 encodes system color brushes as integer resources.
    cls.lpszClassName = editorClass;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return std::nullopt;
    EditorState state{tabs, initialTabIndex, initialItem};
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left - editorWidth) / 2);
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top - editorHeight) / 2);
    const auto window =
        CreateWindowExW(WS_EX_DLGMODALFRAME, editorClass, initialItem ? L"编辑条目" : L"添加条目",
                        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, editorWidth, editorHeight, owner,
                        nullptr, GetModuleHandleW(nullptr), &state);
    if (!window)
        return std::nullopt;
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message{};
    BOOL messageResult = TRUE;
    while (IsWindow(window) && (messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        if (!IsDialogMessageW(window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    SetFocus(owner);
    if (messageResult == 0)
        PostQuitMessage(static_cast<int>(message.wParam));
    return state.takeResult();
}

} // namespace hlaunch::ui
