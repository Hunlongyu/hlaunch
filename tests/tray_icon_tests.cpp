#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/tray_icon.h"

#include <doctest/doctest.h>

#include <Windows.h>

namespace {

class TestWindow final {
public:
    TestWindow()
    {
        constexpr wchar_t className[] = L"HLaunch.TrayIconTestWindow.v1";
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(WNDCLASSEXW);
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = className;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            className,
            L"",
            WS_OVERLAPPED,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            instance,
            nullptr);
    }

    ~TestWindow()
    {
        if (window_) {
            DestroyWindow(window_);
        }
    }

    [[nodiscard]] HWND get() const noexcept
    {
        return window_;
    }

private:
    HWND window_{};
};

} // namespace

TEST_CASE("PLAT-TRAY-001 adds, dispatches and removes the Explorer tray icon")
{
    TestWindow window{};
    REQUIRE(window.get() != nullptr);

    hlaunch::platform::windows::TrayIcon tray{};
    REQUIRE(tray.start(window.get(), LoadIconW(nullptr, IDI_APPLICATION)));
    CHECK(tray.isAdded());
    CHECK(tray.taskbarCreatedMessage() != 0);

    const auto command = tray.handleMessage(
        hlaunch::platform::windows::trayIconCallbackMessage,
        0,
        MAKELPARAM(NIN_SELECT, 1),
        false);
    REQUIRE(command.has_value());
    CHECK(*command == hlaunch::platform::windows::TrayCommand::ToggleLauncher);

    tray.stop();
    CHECK_FALSE(tray.isAdded());
}
