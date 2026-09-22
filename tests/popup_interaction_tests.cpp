// Interactive regression probe: uses a separate foreground process and explicitly
// presents the popup without focus, reproducing the state after a rejected focus
// request independently of OS foreground-lock exemptions. Clicks only test windows.
#include "platform/windows/popup_input_monitor.h"
#include "platform/windows/screen_edge_activation.h"
#include "infrastructure/logging/diagnostic_log.h"
#include "ui/launcher_window.h"

#include <Windows.h>
#include <Ole2.h>
#include <wil/resource.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr wchar_t helperClass[] = L"HLaunch.PopupRegression.Helper";
constexpr wchar_t countProperty[] = L"HLaunch.TestClickCount";

void require(bool value, const char* text)
{
    if (!value) throw std::runtime_error(text);
}

template<class Predicate>
bool pumpUntil(Predicate predicate, DWORD timeout = 2000)
{
    const auto start = GetTickCount64();
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message != WM_QUIT) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (predicate()) return true;
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (GetTickCount64() - start < timeout);
    return false;
}

void pumpFor(DWORD time)
{
    const auto end = GetTickCount64() + time;
    pumpUntil([&] { return GetTickCount64() >= end; }, time + 500);
}

bool clickAt(POINT point)
{
    std::array<INPUT, 3> clicks{};
    for (auto& input : clicks) input.type = INPUT_MOUSE;
    const auto width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const auto height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    clicks[0].mi.dx = MulDiv(point.x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65536, width)
        + 65536 / (2 * width);
    clicks[0].mi.dy = MulDiv(point.y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65536, height)
        + 65536 / (2 * height);
    clicks[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    clicks[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    clicks[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return SendInput(static_cast<UINT>(clicks.size()), clicks.data(), sizeof(INPUT)) == clicks.size();
}

LRESULT CALLBACK helperProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_LBUTTONDOWN) {
        const auto count = reinterpret_cast<UINT_PTR>(GetPropW(window, countProperty));
        SetPropW(window, countProperty, reinterpret_cast<HANDLE>(count + 1));
    }
    if (message == WM_DESTROY) {
        RemovePropW(window, countProperty);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int helperMain()
{
    WNDCLASSW type{};
    type.lpfnWndProc = helperProcedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    type.lpszClassName = helperClass;
    RegisterClassW(&type);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const auto window = CreateWindowExW(0, helperClass, L"HLaunch foreground regression test",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, work.right - 470, work.top + 100,
        420, 420, nullptr, nullptr, type.hInstance, nullptr);
    if (!window) return 2;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

LRESULT CALLBACK activationProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == hlaunch::platform::windows::screenEdgeActivationMessage) {
        auto* edge = reinterpret_cast<hlaunch::platform::windows::ScreenEdgeActivation*>(
            GetPropW(window, L"edge"));
        auto* launcher = reinterpret_cast<hlaunch::ui::LauncherWindow*>(GetPropW(window, L"launcher"));
        if (edge && launcher) {
            if (const auto hit = edge->takePendingActivation()) launcher->showAtScreenEdge(*hit, false);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void runProbe()
{
    const HWND previousForeground = GetForegroundWindow();
    POINT previousCursor{};
    GetCursorPos(&previousCursor);
    const auto restore = wil::scope_exit([&] {
        SetCursorPos(previousCursor.x, previousCursor.y);
        SetForegroundWindow(previousForeground);
    });
    std::wstring executable(32768, L'\0');
    executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
    std::wstring command = L"\"" + executable + L"\" --helper";
    STARTUPINFOW startup{sizeof(startup)};
    wil::unique_process_information process;
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process) != FALSE, "create helper");
    HWND helper{};
    const auto closeHelper = wil::scope_exit([&] {
        if (helper) PostMessageW(helper, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.hProcess, 3000) != WAIT_OBJECT_0)
            TerminateProcess(process.hProcess, 3);
    });
    require(pumpUntil([&] {
        const HWND candidate = FindWindowW(helperClass, nullptr);
        DWORD processId{};
        GetWindowThreadProcessId(candidate, &processId);
        if (processId == process.dwProcessId) helper = candidate;
        return helper != nullptr;
    }), "helper ready");
    RECT helperBounds{};
    GetWindowRect(helper, &helperBounds);
    const POINT outside{helperBounds.left + 100, helperBounds.top + 100};
    require(clickAt(outside), "click helper");
    require(pumpUntil([&] { return GetForegroundWindow() == helper
        && GetPropW(helper, countProperty) != nullptr; }), "helper foreground and click processed");

    hlaunch::ui::LauncherWindow launcher;
    require(launcher.create(GetModuleHandleW(nullptr), {}, false, {},
        [](const hlaunch::core::LaunchItem&) {}), "create launcher");
    WNDCLASSW type{};
    type.lpfnWndProc = activationProcedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"HLaunch.PopupRegression.Activation";
    RegisterClassW(&type);
    const auto activation = CreateWindowExW(0, type.lpszClassName, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, nullptr);
    require(activation != nullptr, "create activation receiver");
    const auto destroyActivation = wil::scope_exit([&] { DestroyWindow(activation); });
    hlaunch::platform::windows::ScreenEdgeActivation edge;
    SetPropW(activation, L"edge", reinterpret_cast<HANDLE>(&edge));
    SetPropW(activation, L"launcher", reinterpret_cast<HANDLE>(&launcher));
    hlaunch::core::ScreenEdgeConfig config;
    config.enabled = true;
    config.zones = {hlaunch::core::ScreenEdgeZone::TopLeft};
    config.edgeMode = hlaunch::core::ScreenEdgeMode::EveryMonitor;
    require(edge.start({activation, launcher.handle()}, config), "start edge detector");
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const POINT corner{monitor.rcMonitor.left, monitor.rcMonitor.top};
    for (int iteration = 0; iteration < 30; ++iteration) {
        require(SetCursorPos(corner.x, corner.y) != FALSE, "move to corner");
        const bool appeared = pumpUntil([&] { return launcher.isVisible(); });
        if (!appeared) {
            POINT actual{};
            GetCursorPos(&actual);
            std::printf("edge timeout iteration=%d cursor=%ld,%ld corner=%ld,%ld foreground=%p helper=%p\n",
                iteration, actual.x, actual.y, corner.x, corner.y, GetForegroundWindow(), helper);
        }
        require(appeared, "edge show");
        require((GetWindowLongPtrW(launcher.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
                "popup topmost");
        require(GetForegroundWindow() == helper, "popup visible without foreground");
        const auto clicks = reinterpret_cast<UINT_PTR>(GetPropW(helper, countProperty));
        require(clickAt(outside), "external click");
        require(pumpUntil([&] { return !launcher.isVisible(); }), "outside click hides popup");
        require(pumpUntil([&] {
            return reinterpret_cast<UINT_PTR>(GetPropW(helper, countProperty)) == clicks + 1;
        }), "external click delivered unchanged");
        pumpFor(120); // allow the edge sampler to observe leaving before re-entry
    }
    edge.stop();
    std::puts("PASS: 30 real edge / visible-without-foreground / outside-click / re-entry cycles");

    launcher.show();
    // show() is cursor-centred, so use its actual bounds for owned interactions.
    RECT bounds{};
    GetWindowRect(launcher.handle(), &bounds);
    require(clickAt({bounds.left + 20, bounds.top + 30}), "click popup");
    require(pumpUntil([&] { return GetForegroundWindow() == launcher.handle(); }), "popup focus");
    const HWND owned = CreateWindowExW(0, L"STATIC", L"Owned settings test",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, monitor.rcWork.left + 480, monitor.rcWork.top + 60,
        300, 180, launcher.handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
    require(owned != nullptr, "owned dialog");
    const auto destroyOwned = wil::scope_exit([&] { DestroyWindow(owned); });
    const HWND child = CreateWindowExW(0, L"BUTTON", L"Settings control", WS_CHILD | WS_VISIBLE,
        10, 10, 120, 30, owned, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(hlaunch::platform::windows::belongsToPopup(child, launcher.handle()), "child belongs to popup");
    require(!hlaunch::platform::windows::belongsToPopup(helper, launcher.handle()), "external family rejected");
    RECT childBounds{};
    GetWindowRect(child, &childBounds);
    require(clickAt({childBounds.left + 15, childBounds.top + 10}), "click settings control");
    pumpFor(160);
    require(launcher.isVisible(), "settings click preserves popup");
    require(GetForegroundWindow() == owned, "settings stays foreground");
    launcher.showAtScreenEdge({hlaunch::core::ScreenEdgeZone::TopLeft,
        {corner.x, corner.y}, {monitor.rcMonitor.left, monitor.rcMonitor.top,
        monitor.rcMonitor.right, monitor.rcMonitor.bottom},
        {monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right, monitor.rcWork.bottom}});
    require(GetForegroundWindow() == owned, "edge re-entry preserves settings focus");
    EnableWindow(launcher.handle(), FALSE);
    require(clickAt(outside), "click during modal interaction");
    pumpFor(160);
    require(launcher.isVisible(), "modal owner remains visible");
    EnableWindow(launcher.handle(), TRUE);
    ShowWindow(owned, SW_HIDE);
    require(clickAt({corner.x + 25, corner.y + 30}), "focus popup for menu");
    require(pumpUntil([&] { return GetForegroundWindow() == launcher.handle(); }), "menu owner focus");
    wil::unique_hmenu menu{CreatePopupMenu()};
    AppendMenuW(menu.get(), MF_STRING, 1, L"Regression menu item");
    const POINT menuPoint{monitor.rcWork.left + 700, monitor.rcWork.top + 60};
    std::jthread menuClick([&] {
        Sleep(120);
        clickAt({menuPoint.x + 30, menuPoint.y + 12});
        // Bounds the nested menu loop even if desktop input was interrupted.
        Sleep(300);
        PostMessageW(launcher.handle(), WM_CANCELMODE, 0, 0);
    });
    const auto selected = TrackPopupMenuEx(menu.get(), TPM_RETURNCMD,
        menuPoint.x, menuPoint.y, launcher.handle(), nullptr);
    menuClick.join();
    require(selected == 1, "native menu item receives click");
    pumpFor(160);
    require(launcher.isVisible(), "native menu click preserves popup");
    // Foreground notification must also work without a mouse click (Alt+Tab class of transition).
    AllowSetForegroundWindow(process.dwProcessId);
    SetForegroundWindow(helper);
    require(pumpUntil([&] { return !launcher.isVisible(); }), "foreground switch hides popup");
    std::puts("PASS: owned dialog/control, modal protection, native menu, foreground dismissal");
}
} // namespace

int main(int argc, char**)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc > 1) return helperMain();
    const auto logDirectory = std::filesystem::temp_directory_path()
        / (L"HLaunch-PopupProbe-" + std::to_wstring(GetCurrentProcessId()));
    const auto log = hlaunch::infrastructure::logging::initialize({.directory = logDirectory});
    if (log) std::printf("log=%s\n", log->string().c_str());
    const auto stopLog = wil::scope_exit([] { hlaunch::infrastructure::logging::shutdown(); });
    const auto result = OleInitialize(nullptr);
    const auto uninitialize = wil::scope_exit([&] { if (SUCCEEDED(result)) OleUninitialize(); });
    try {
        runProbe();
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
