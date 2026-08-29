#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/tray_icon.h"

#include "app_version.h"

#include <doctest/doctest.h>

#include <Windows.h>

#include <array>

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

TEST_CASE("PLAT-TRAY-001 tooltip shows the product name and current version")
{
    const auto tooltip = hlaunch::platform::windows::trayIconTooltipText();
    CHECK(tooltip.starts_with(L"HLaunch\nv"));
    CHECK(tooltip.substr(tooltip.find(L'v') + 1U) == hlaunch::applicationVersionWide);
}

TEST_CASE("PLAT-TRAY-001 context menu exposes checkable startup state")
{
    const auto menu = hlaunch::platform::windows::createTrayContextMenu(false, true);
    REQUIRE(menu);
    CHECK(GetMenuItemCount(menu.get()) == 5);
    CHECK(GetMenuItemID(menu.get(), 0) == hlaunch::platform::windows::trayToggleMenuId);
    CHECK(GetMenuItemID(menu.get(), 1) == hlaunch::platform::windows::trayStartupMenuId);
    CHECK(GetMenuItemID(menu.get(), 2) == hlaunch::platform::windows::traySettingsMenuId);
    CHECK(GetMenuItemID(menu.get(), 4) == hlaunch::platform::windows::trayExitMenuId);
    CHECK((GetMenuState(menu.get(), 1, MF_BYPOSITION) & MF_CHECKED) != 0U);

    std::array<wchar_t, 32> startupLabel{};
    REQUIRE(GetMenuStringW(
        menu.get(),
        1,
        startupLabel.data(),
        static_cast<int>(startupLabel.size()),
        MF_BYPOSITION) > 0);
    CHECK(std::wstring_view{startupLabel.data()} == L"开机自启");

    const auto disabledMenu =
        hlaunch::platform::windows::createTrayContextMenu(true, std::nullopt);
    REQUIRE(disabledMenu);
    CHECK((GetMenuState(disabledMenu.get(), 1, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) != 0U);

    const auto uncheckedMenu =
        hlaunch::platform::windows::createTrayContextMenu(true, false);
    REQUIRE(uncheckedMenu);
    CHECK((GetMenuState(uncheckedMenu.get(), 1, MF_BYPOSITION) & MF_CHECKED) == 0U);
}
