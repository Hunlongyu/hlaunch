#include "ui/item_editor_dialog.h"

#include "ui/theme.h"

#include <algorithm>
#include <array>
#include <windowsx.h>
#include <wil/resource.h>
#include <string>
#include <string_view>

namespace hlaunch::ui {
namespace {

constexpr wchar_t editorClass[] = L"HLaunch.ItemEditor.v1";
constexpr int editorWidth = 560;
constexpr int editorHeight = 660;
constexpr int headerHeight = 48;
constexpr int contentOffset = 48;
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
                const core::LaunchItem *item, const core::ThemeMode themeMode)
        : tabs_(tabs), initialTab_(initialTab), initialItem_(item), themeMode_(themeMode),
          palette_(paletteFor(themeMode)),
          backgroundBrush_(CreateSolidBrush(toColorRef(palette_.background))),
          surfaceBrush_(CreateSolidBrush(toColorRef(palette_.surface)))
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
            applyNativeWindowTheme(window_, themeMode_);
            createControls();
            return 0;
        }
        if (message == WM_PAINT)
        {
            paint();
            return 0;
        }
        if (message == WM_ERASEBKGND)
            return TRUE;
        if (message == WM_NCHITTEST)
        {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window_, &point);
            if (point.y >= 0 && point.y < headerHeight && !closeHit(point))
                return HTCAPTION;
            return HTCLIENT;
        }
        if (message == WM_LBUTTONUP)
        {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (closeHit(point))
                finish();
            return 0;
        }
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT
            || message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN)
        {
            const auto context = reinterpret_cast<HDC>(wParam); // NOLINT(performance-no-int-to-ptr): Win32 passes HDC in WPARAM.
            SetTextColor(context, toColorRef(palette_.text));
            SetBkColor(context, toColorRef(
                message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN
                                               ? palette_.background
                                               : palette_.surface));
            return reinterpret_cast<LRESULT>(
                message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN
                                                 ? backgroundBrush_.get()
                                                 : surfaceBrush_.get()); // NOLINT(performance-no-int-to-ptr): Win32 expects HBRUSH in LRESULT.
        }
        if (message == WM_DRAWITEM)
        {
            const auto &item = *reinterpret_cast<const DRAWITEMSTRUCT *>(lParam); // NOLINT(performance-no-int-to-ptr): Win32 LPARAM carries DRAWITEMSTRUCT*.
            if (item.CtlType == ODT_COMBOBOX)
                drawComboItem(item);
            else
                drawButton(item);
            return TRUE;
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
            applyNativeControlTheme(control, themeMode_);
            return control;
        };
        auto label = [&](const wchar_t *text, int y) {
            add(L"STATIC", text, 0, 20, y + contentOffset, 120, 20, 0);
        };
        const int fieldX = 145;
        label(L"名称", 22);
        name_ =
            add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, fieldX, 18 + contentOffset, 380, 25, idName);
        label(L"类型", 60);
        type_ = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, fieldX, 56 + contentOffset, 220, 200, idType);
        for (const wchar_t *value : {L"应用", L"文件", L"文件夹", L"网址", L"快捷方式"})
            SendMessageW(type_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        label(L"目标", 98);
        target_ = add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, fieldX, 94 + contentOffset, 380, 25,
                      idTarget);
        label(L"参数（每行一个）", 136);
        arguments_ =
            add(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
                fieldX, 132 + contentOffset, 380, 100, idArguments);
        label(L"工作目录（可选）", 250);
        workingDirectory_ = add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, fieldX, 246 + contentOffset,
                                380, 25, idWorkingDirectory);
        label(L"图标路径（可选）", 288);
        icon_ = add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, fieldX, 284 + contentOffset, 380, 25,
                    idIcon);
        label(L"所属分类", 326);
        tab_ = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, fieldX, 322 + contentOffset, 220, 200, idTab);
        for (const auto &tab : tabs_)
        {
            const auto name = utf8ToWide(tab.name);
            SendMessageW(tab_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        administrator_ = add(L"BUTTON", L"以管理员身份运行", BS_AUTOCHECKBOX | WS_TABSTOP, fieldX,
                             366 + contentOffset, 220, 25, idAdministrator);
        add(L"BUTTON", L"保存", BS_OWNERDRAW | WS_TABSTOP, 325, 573, 95, 34, idSave);
        add(L"BUTTON", L"取消", BS_OWNERDRAW | WS_TABSTOP, 430, 573, 95, 34, idCancel);
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

    [[nodiscard]] bool closeHit(const POINT point) const noexcept
    {
        return point.x >= editorWidth - 48 && point.x < editorWidth
            && point.y >= 0 && point.y < headerHeight;
    }

    void paint()
    {
        PAINTSTRUCT paintState{};
        const auto context = BeginPaint(window_, &paintState);
        RECT client{};
        GetClientRect(window_, &client);
        FillRect(context, &client, backgroundBrush_.get());

        const auto oldFont = SelectObject(context, GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(context, TRANSPARENT);
        SetTextColor(context, toColorRef(palette_.text));
        RECT titleBounds{52, 0, editorWidth - 56, headerHeight};
        DrawTextW(
            context,
            initialItem_ ? L"编辑条目" : L"添加条目",
            -1,
            &titleBounds,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const auto accentBrush = CreateSolidBrush(toColorRef(palette_.accent));
        RECT logo{20, 15, 38, 33};
        FillRect(context, &logo, accentBrush);
        DeleteObject(accentBrush);

        const auto pen = CreatePen(PS_SOLID, 2, toColorRef(palette_.textMuted));
        const auto oldPen = SelectObject(context, pen);
        MoveToEx(context, editorWidth - 30, 18, nullptr);
        LineTo(context, editorWidth - 20, 28);
        MoveToEx(context, editorWidth - 20, 18, nullptr);
        LineTo(context, editorWidth - 30, 28);
        SelectObject(context, oldPen);
        DeleteObject(pen);
        SelectObject(context, oldFont);
        EndPaint(window_, &paintState);
    }

    void drawButton(const DRAWITEMSTRUCT &item) const
    {
        const bool saveButton = item.CtlID == idSave;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const auto fillColor = saveButton && !pressed ? palette_.accent : palette_.elevated;
        const auto fill = CreateSolidBrush(toColorRef(fillColor));
        const auto border = CreatePen(PS_SOLID, 1, toColorRef(saveButton ? palette_.accent : palette_.border));
        const auto oldBrush = SelectObject(item.hDC, fill);
        const auto oldPen = SelectObject(item.hDC, border);
        RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, 10, 10);
        SelectObject(item.hDC, oldBrush);
        SelectObject(item.hDC, oldPen);
        DeleteObject(fill);
        DeleteObject(border);

        wchar_t text[32]{};
        GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
        SetBkMode(item.hDC, TRANSPARENT);
        const auto buttonText = saveButton
            ? (themeMode_ == core::ThemeMode::Dark ? palette_.background : 0xFFFFFFU)
            : palette_.text;
        SetTextColor(item.hDC, toColorRef(buttonText));
        RECT bounds = item.rcItem;
        DrawTextW(item.hDC, text, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if ((item.itemState & ODS_FOCUS) != 0)
        {
            InflateRect(&bounds, -4, -4);
            DrawFocusRect(item.hDC, &bounds);
        }
    }

    void drawComboItem(const DRAWITEMSTRUCT &item) const
    {
        const bool dropdownSelection = (item.itemState & ODS_SELECTED) != 0
            && (item.itemState & ODS_COMBOBOXEDIT) == 0;
        const auto background = dropdownSelection ? palette_.accent : palette_.surface;
        const auto brush = CreateSolidBrush(toColorRef(background));
        FillRect(item.hDC, &item.rcItem, brush);
        DeleteObject(brush);

        const auto selectedIndex = item.itemID == static_cast<UINT>(-1)
            ? static_cast<UINT>(SendMessageW(item.hwndItem, CB_GETCURSEL, 0, 0))
            : item.itemID;
        wchar_t text[256]{};
        if (selectedIndex != static_cast<UINT>(CB_ERR))
            SendMessageW(item.hwndItem, CB_GETLBTEXT, selectedIndex, reinterpret_cast<LPARAM>(text));
        SetBkMode(item.hDC, TRANSPARENT);
        const auto textColor = dropdownSelection
            ? (themeMode_ == core::ThemeMode::Dark ? palette_.background : 0xFFFFFFU)
            : palette_.text;
        SetTextColor(item.hDC, toColorRef(textColor));
        RECT bounds = item.rcItem;
        bounds.left += 6;
        DrawTextW(item.hDC, text, -1, &bounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if ((item.itemState & ODS_FOCUS) != 0)
        {
            bounds = item.rcItem;
            InflateRect(&bounds, -2, -2);
            DrawFocusRect(item.hDC, &bounds);
        }
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
    core::ThemeMode themeMode_{core::ThemeMode::Dark};
    ThemePalette palette_{};
    wil::unique_hbrush backgroundBrush_{};
    wil::unique_hbrush surfaceBrush_{};
    std::optional<ItemEditorResult> result_{};
};

} // namespace

std::optional<ItemEditorResult> ItemEditorDialog::show(HWND owner,
                                                       const std::vector<core::Tab> &tabs,
                                                       std::size_t initialTabIndex,
                                                       const core::LaunchItem *initialItem,
                                                       const core::ThemeMode themeMode)
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
    cls.hbrBackground = nullptr;
    cls.lpszClassName = editorClass;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return std::nullopt;
    EditorState state{tabs, initialTabIndex, initialItem, themeMode};
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left - editorWidth) / 2);
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top - editorHeight) / 2);
    const auto window =
        CreateWindowExW(WS_EX_TOOLWINDOW, editorClass, initialItem ? L"编辑条目" : L"添加条目",
                        WS_POPUP, x, y, editorWidth, editorHeight, owner,
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
