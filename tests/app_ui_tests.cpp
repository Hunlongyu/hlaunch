#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "app/command_line.h"
#include "core/data_validation.h"
#include "ui/clipboard.h"
#include "platform/windows/activation_command.h"
#include "platform/windows/app_identity.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/window_effects.h"
#include "ui/item_context_menu.h"
#include "ui/launcher_context_menu.h"
#include "ui/launcher_layout.h"
#include "ui/launcher_window.h"
#include "ui/settings_window.h"
#include "ui/system_appearance.h"
#include "ui/visual_style.h"

#include <Ole2.h>
#include <CommCtrl.h>
#include <UIAutomationClient.h>
#include <doctest/doctest.h>
#include <dwmapi.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <array>
#include <atomic>
#include <chrono>
#include <expected>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::atomic<WPARAM> receivedActivationCommand{};

LRESULT CALLBACK activationTestWindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    if (message == hlaunch::platform::windows::activationMessageId()) {
        receivedActivationCommand.store(wParam);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

class OleTestApartment final {
public:
    OleTestApartment() : result_(OleInitialize(nullptr)) {}
    ~OleTestApartment()
    {
        if (SUCCEEDED(result_)) {
            OleUninitialize();
        }
    }

private:
    HRESULT result_{};
};

OleTestApartment testOleApartment{};

hlaunch::core::LaunchItem searchableItem(std::string id, std::string name)
{
    return hlaunch::core::LaunchItem{
        .id = std::move(id),
        .name = std::move(name),
        .target = "not-used-by-test",
    };
}

void clearPendingQuitMessages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

POINT centerInPixels(const HWND window, const hlaunch::ui::RectDip& rectangle)
{
    const auto dpi = GetDpiForWindow(window);
    return POINT{
        static_cast<LONG>((rectangle.x + (rectangle.width / 2.0F)) * static_cast<float>(dpi) /
                          96.0F),
        static_cast<LONG>((rectangle.y + (rectangle.height / 2.0F)) * static_cast<float>(dpi) /
                          96.0F),
    };
}

hlaunch::ui::LauncherLayout launcherLayoutFor(const HWND window, const std::size_t itemCount)
{
    RECT client{};
    GetClientRect(window, &client);
    const auto dpi = GetDpiForWindow(window);
    return hlaunch::ui::calculateLauncherLayout({
        .clientWidthDip = static_cast<float>(client.right) * 96.0F / static_cast<float>(dpi),
        .clientHeightDip = static_cast<float>(client.bottom) * 96.0F / static_cast<float>(dpi),
        .itemCount = itemCount,
    });
}

} // namespace

TEST_CASE("PROD-ITEM-001 item context menu exposes complete grouped actions")
{
    const hlaunch::core::ItemsDocument document{
        .tabs =
            {
                hlaunch::core::Tab{
                    .id = "11111111-1111-4111-8111-111111111111",
                    .name = "默认",
                    .items = {searchableItem("22222222-2222-4222-8222-222222222222", "示例")},
                },
                hlaunch::core::Tab{
                    .id = "33333333-3333-4333-8333-333333333333",
                    .name = "工作&开发",
                },
            },
    };

    const auto menu = hlaunch::ui::createItemContextMenu(document, {0, 0});
    REQUIRE(menu);
    CHECK(GetMenuItemCount(menu.get()) == 11);
    CHECK(GetMenuItemID(menu.get(), 0) == static_cast<UINT>(hlaunch::ui::ItemContextCommand::Open));
    CHECK(GetMenuItemID(menu.get(), 1) ==
          static_cast<UINT>(hlaunch::ui::ItemContextCommand::RunAsAdministrator));
    CHECK(GetMenuItemID(menu.get(), 6) ==
          static_cast<UINT>(hlaunch::ui::ItemContextCommand::Insert));
    CHECK(GetMenuItemID(menu.get(), 8) ==
          static_cast<UINT>(hlaunch::ui::ItemContextCommand::Delete));
    CHECK(GetMenuItemID(menu.get(), 10) ==
          static_cast<UINT>(hlaunch::ui::ItemContextCommand::Properties));
    CHECK((GetMenuState(menu.get(), 1, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) == 0U);

    const auto copyMenu = GetSubMenu(menu.get(), 4);
    REQUIRE(copyMenu != nullptr);
    CHECK(GetMenuItemCount(copyMenu) == 3);
    CHECK((GetMenuState(copyMenu, 0, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) == 0U);
    CHECK(GetMenuItemID(copyMenu, 2) ==
          static_cast<UINT>(hlaunch::ui::ItemContextCommand::CopyCommandLine));

    const auto moveMenu = GetSubMenu(menu.get(), 7);
    REQUIRE(moveMenu != nullptr);
    CHECK(GetMenuItemCount(moveMenu) == 2);
    CHECK((GetMenuState(moveMenu, 0, MF_BYPOSITION) & MF_CHECKED) != 0U);
    CHECK((GetMenuState(moveMenu, 0, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) != 0U);
    CHECK((GetMenuState(moveMenu, 1, MF_BYPOSITION) & MF_CHECKED) == 0U);
    CHECK((GetMenuState(moveMenu, 1, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) == 0U);
    CHECK(GetMenuItemID(moveMenu, 1) == hlaunch::ui::moveToTabMenuCommand(1));
    CHECK(hlaunch::ui::moveToTabIndexFromMenuCommand(hlaunch::ui::moveToTabMenuCommand(1),
                                                     document.tabs.size()) == 1);
}

TEST_CASE("PROD-ITEM-001 copies Unicode item text through the Windows clipboard")
{
    constexpr std::wstring_view expected = L"HLaunch 中文剪贴板";
    REQUIRE(hlaunch::ui::copyUnicodeTextToClipboard(nullptr, expected));
    REQUIRE(OpenClipboard(nullptr));
    const auto clipboardCleanup = wil::scope_exit([] { CloseClipboard(); });
    const HANDLE storage = GetClipboardData(CF_UNICODETEXT);
    REQUIRE(storage != nullptr);
    const auto* actual = static_cast<const wchar_t*>(GlobalLock(static_cast<HGLOBAL>(storage)));
    REQUIRE(actual != nullptr);
    CHECK(std::wstring_view{actual} == expected);
    GlobalUnlock(static_cast<HGLOBAL>(storage));
}

TEST_CASE("PROD-GRID-001 pin button toggles topmost state")
{
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr), {}, false, {},
        [](const hlaunch::core::LaunchItem&) {}));
    const auto layout = launcherLayoutFor(launcher.handle(), 0U);
    const auto pinPoint = centerInPixels(launcher.handle(), layout.pinButton);

    CHECK((GetWindowLongPtrW(launcher.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
    SendMessageW(
        launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(pinPoint.x, pinPoint.y));
    CHECK((GetWindowLongPtrW(launcher.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    SendMessageW(
        launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(pinPoint.x, pinPoint.y));
    CHECK((GetWindowLongPtrW(launcher.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);

    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 pinned launcher stays visible after a successful item launch")
{
    clearPendingQuitMessages();
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr), {}, false, {},
        [](const hlaunch::core::LaunchItem&) {}));

    launcher.show();
    REQUIRE(launcher.isVisible());
    launcher.hideAfterSuccessfulLaunchIfNeeded();
    CHECK_FALSE(launcher.isVisible());

    launcher.show();
    const auto layout = launcherLayoutFor(launcher.handle(), 0U);
    const auto pinPoint = centerInPixels(launcher.handle(), layout.pinButton);
    SendMessageW(
        launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(pinPoint.x, pinPoint.y));
    REQUIRE((GetWindowLongPtrW(launcher.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);

    launcher.hideAfterSuccessfulLaunchIfNeeded();
    CHECK(launcher.isVisible());

    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_ESCAPE, 0);
    CHECK_FALSE(launcher.isVisible());

    launcher.show();
    REQUIRE(launcher.isVisible());
    REQUIRE((GetWindowLongPtrW(launcher.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    SendMessageW(launcher.handle(), WM_CLOSE, 0, 0);
    CHECK_FALSE(launcher.isVisible());

    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 launcher chrome empty slots and tabs expose separate "
          "menus")
{
    const auto launcher = hlaunch::ui::createLauncherContextMenu(true);
    REQUIRE(launcher);
    CHECK(GetMenuItemCount(launcher.get()) == 7);
    CHECK(GetMenuItemID(launcher.get(), 0) ==
          static_cast<UINT>(hlaunch::ui::LauncherContextCommand::TogglePin));
    CHECK((GetMenuState(launcher.get(), 0, MF_BYPOSITION) & MF_CHECKED) != 0U);
    std::array<wchar_t, 64> pinLabel{};
    REQUIRE(GetMenuStringW(
        launcher.get(), 0, pinLabel.data(), static_cast<int>(pinLabel.size()), MF_BYPOSITION) > 0);
    CHECK(std::wstring_view{pinLabel.data()}.starts_with(L"置顶窗口"));
    CHECK(GetMenuItemID(launcher.get(), 4) ==
          static_cast<UINT>(hlaunch::ui::LauncherContextCommand::Settings));
    std::array<wchar_t, 64> settingsLabel{};
    REQUIRE(GetMenuStringW(
        launcher.get(), 4, settingsLabel.data(), static_cast<int>(settingsLabel.size()),
        MF_BYPOSITION) > 0);
    CHECK(std::wstring_view{settingsLabel.data()} == L"设置...\tCtrl+O");

    const auto emptySlot = hlaunch::ui::createEmptySlotContextMenu();
    REQUIRE(emptySlot);
    CHECK(GetMenuItemCount(emptySlot.get()) == 3);
    CHECK(GetMenuItemID(emptySlot.get(), 0) ==
          static_cast<UINT>(hlaunch::ui::EmptySlotContextCommand::RegisterItem));
    CHECK(GetMenuItemID(emptySlot.get(), 2) ==
          static_cast<UINT>(hlaunch::ui::EmptySlotContextCommand::InsertSlot));

    const auto tab = hlaunch::ui::createTabContextMenu(0, 1);
    REQUIRE(tab);
    CHECK(GetMenuItemID(tab.get(), 0) ==
          static_cast<UINT>(hlaunch::ui::TabContextCommand::AddPage));
    CHECK((GetMenuState(tab.get(), 1, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) != 0U);
    CHECK((GetMenuState(tab.get(), 4, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) != 0U);
    const auto populatedTab = hlaunch::ui::createTabContextMenu(0, 1, true);
    REQUIRE(populatedTab);
    CHECK((GetMenuState(populatedTab.get(), 4, MF_BYPOSITION)
           & (MF_DISABLED | MF_GRAYED)) == 0U);
    CHECK(GetMenuItemID(tab.get(), 6) ==
          static_cast<UINT>(hlaunch::ui::TabContextCommand::Rename));
    std::array<wchar_t, 32> renameLabel{};
    REQUIRE(GetMenuStringW(
        tab.get(), 6, renameLabel.data(), static_cast<int>(renameLabel.size()), MF_BYPOSITION) > 0);
    CHECK(std::wstring_view{renameLabel.data()} == L"重命名");
    const auto singleSortMenu = GetSubMenu(tab.get(), 3);
    REQUIRE(singleSortMenu != nullptr);
    CHECK((GetMenuState(singleSortMenu, 0, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) != 0U);
    CHECK((GetMenuState(singleSortMenu, 1, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) != 0U);

    const auto middleTab = hlaunch::ui::createTabContextMenu(1, 3);
    REQUIRE(middleTab);
    const auto middleSortMenu = GetSubMenu(middleTab.get(), 3);
    REQUIRE(middleSortMenu != nullptr);
    CHECK(GetMenuItemID(middleSortMenu, 0) ==
          static_cast<UINT>(hlaunch::ui::TabContextCommand::MovePageLeft));
    CHECK(GetMenuItemID(middleSortMenu, 1) ==
          static_cast<UINT>(hlaunch::ui::TabContextCommand::MovePageRight));
    CHECK((GetMenuState(middleSortMenu, 0, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) == 0U);
    CHECK((GetMenuState(middleSortMenu, 1, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) == 0U);
}

TEST_CASE("PROD-GRID-001 empty tabs delete immediately while populated tabs require confirmation")
{
    using hlaunch::ui::TabDeleteDisposition;
    const hlaunch::core::ItemsDocument document{
        .tabs = {
            hlaunch::core::Tab{
                .id = "11111111-1111-4111-8111-111111111111",
                .name = "Empty",
            },
            hlaunch::core::Tab{
                .id = "22222222-2222-4222-8222-222222222222",
                .name = "Populated",
                .items = {searchableItem(
                    "33333333-3333-4333-8333-333333333333", "Item")},
            },
        },
    };

    CHECK(hlaunch::ui::tabDeleteDisposition(document, 0U)
          == TabDeleteDisposition::Immediate);
    CHECK(hlaunch::ui::tabDeleteDisposition(document, 1U)
          == TabDeleteDisposition::ConfirmationRequired);
    CHECK(hlaunch::ui::tabDeleteDisposition(document, 2U)
          == TabDeleteDisposition::Unavailable);

    auto singleTab = document;
    singleTab.tabs.resize(1U);
    CHECK(hlaunch::ui::tabDeleteDisposition(singleTab, 0U)
          == TabDeleteDisposition::Unavailable);
}

TEST_CASE("UI-SETTINGS-001 settings groups general and activation options and reuses window")
{
    auto selectedAppearance = hlaunch::core::AppearanceConfig{};
    auto selectedActivation = hlaunch::core::ActivationConfig{};
    auto legacyActivation = hlaunch::core::ActivationConfig{};
    legacyActivation.screenEdge.thicknessDip = 12.0;
    legacyActivation.screenEdge.cornerSizeDip = 48.0;
    legacyActivation.screenEdge.dwellMs = 900;
    legacyActivation.screenEdge.pollMs = 50;
    legacyActivation.screenEdge.cooldownMs = 4'000;
    bool selectedDiagnostics{true};
    hlaunch::ui::SettingsWindow settings{};

    REQUIRE(settings.show(
        GetModuleHandleW(nullptr), nullptr, hlaunch::core::AppearanceConfig{},
        legacyActivation, true,
        [&selectedAppearance](const hlaunch::core::AppearanceConfig& appearance) {
            selectedAppearance = appearance;
            return true;
        },
        [&selectedActivation](const hlaunch::core::ActivationConfig& activation)
            -> std::expected<void, std::wstring> {
            selectedActivation = activation;
            return {};
        },
        [&selectedDiagnostics](const bool enabled) -> std::expected<void, std::wstring> {
            selectedDiagnostics = enabled;
            return {};
        }));
    wchar_t settingsTitle[64]{};
    GetWindowTextW(settings.handle(), settingsTitle, static_cast<int>(std::size(settingsTitle)));
    CHECK(std::wstring_view{settingsTitle} == L"HLaunch 设置");
    REQUIRE(settings.handle() != nullptr);
    CHECK(settings.isVisible());
    const auto initialWindow = settings.handle();

    const auto tab = GetDlgItem(settings.handle(), 2000);
    const auto backdropCombo = GetDlgItem(settings.handle(), 2001);
    const auto tabPageBackground = GetDlgItem(settings.handle(), 2002);
    const auto opacitySlider = GetDlgItem(settings.handle(), 2004);
    const auto opacityEdit = GetDlgItem(settings.handle(), 2005);
    const auto hotkeyKey = GetDlgItem(settings.handle(), 2015);
    const auto controlCheck = GetDlgItem(settings.handle(), 2012);
    const auto diagnosticLoggingEnabled = GetDlgItem(settings.handle(), 2050);
    const auto processBlocklist = GetDlgItem(settings.handle(), 2033);
    const auto processAllowlist = GetDlgItem(settings.handle(), 2034);
    const auto bottomRightZone = GetDlgItem(settings.handle(), 2026);
    REQUIRE(hotkeyKey != nullptr);
    REQUIRE(controlCheck != nullptr);
    CHECK(GetDlgItem(settings.handle(), 2040) == nullptr);
    REQUIRE(diagnosticLoggingEnabled != nullptr);
    REQUIRE(processBlocklist != nullptr);
    REQUIRE(processAllowlist != nullptr);
    REQUIRE(bottomRightZone != nullptr);
    REQUIRE(tab != nullptr);
    REQUIRE(tabPageBackground != nullptr);
    REQUIRE(backdropCombo != nullptr);
    REQUIRE(opacitySlider != nullptr);
    REQUIRE(opacityEdit != nullptr);
    REQUIRE(GetDlgItem(settings.handle(), 2003) != nullptr);
    for (const int removedControlId : {2028, 2029, 2030, 2031, 2032}) {
        CHECK(GetDlgItem(settings.handle(), removedControlId) == nullptr);
    }
    const auto findChildByText = [&settings](const std::wstring_view expected) {
        struct Context {
            std::wstring_view expected;
            HWND match{};
        } context{expected};
        EnumChildWindows(
            settings.handle(),
            [](const HWND child, const LPARAM value) noexcept -> BOOL {
                auto& context = *reinterpret_cast<Context*>(value);
                wchar_t text[128]{};
                GetWindowTextW(child, text, static_cast<int>(std::size(text)));
                if (context.expected == text) {
                    context.match = child;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&context));
        return context.match;
    };
    const auto backdropLabel = findChildByText(L"背景效果：");
    const auto screenEdgeGroup = findChildByText(L"屏幕边缘");
    const auto settingsStatus = findChildByText(L"修改后选择“应用”或“确定”保存设置。");
    REQUIRE(backdropLabel != nullptr);
    REQUIRE(screenEdgeGroup != nullptr);
    REQUIRE(settingsStatus != nullptr);
    CHECK((GetWindowLongPtrW(backdropLabel, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0);
    CHECK((GetWindowLongPtrW(diagnosticLoggingEnabled, GWL_EXSTYLE)
           & WS_EX_TRANSPARENT)
          != 0);
    const auto labelDc = GetDC(backdropLabel);
    REQUIRE(labelDc != nullptr);
    CHECK(reinterpret_cast<HBRUSH>(SendMessageW(
              settings.handle(), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(labelDc),
              reinterpret_cast<LPARAM>(backdropLabel)))
          == reinterpret_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH)));
    CHECK(GetBkMode(labelDc) == TRANSPARENT);
    ReleaseDC(backdropLabel, labelDc);
    const auto pageDc = GetDC(tabPageBackground);
    REQUIRE(pageDc != nullptr);
    CHECK(reinterpret_cast<HBRUSH>(SendMessageW(
              settings.handle(), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(pageDc),
              reinterpret_cast<LPARAM>(tabPageBackground)))
          == GetSysColorBrush(COLOR_BTNFACE));
    CHECK(GetBkMode(pageDc) == OPAQUE);
    CHECK(GetBkColor(pageDc) == GetSysColor(COLOR_BTNFACE));
    ReleaseDC(tabPageBackground, pageDc);
    const auto checkboxDc = GetDC(diagnosticLoggingEnabled);
    REQUIRE(checkboxDc != nullptr);
    CHECK(reinterpret_cast<HBRUSH>(SendMessageW(
              settings.handle(), WM_CTLCOLORBTN, reinterpret_cast<WPARAM>(checkboxDc),
              reinterpret_cast<LPARAM>(diagnosticLoggingEnabled)))
          == reinterpret_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH)));
    CHECK(GetBkMode(checkboxDc) == TRANSPARENT);
    ReleaseDC(diagnosticLoggingEnabled, checkboxDc);
    const auto sliderDc = GetDC(opacitySlider);
    REQUIRE(sliderDc != nullptr);
    CHECK(reinterpret_cast<HBRUSH>(SendMessageW(
              settings.handle(), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(sliderDc),
              reinterpret_cast<LPARAM>(opacitySlider)))
          == GetSysColorBrush(COLOR_BTNFACE));
    CHECK(GetBkMode(sliderDc) == OPAQUE);
    CHECK(GetBkColor(sliderDc) == GetSysColor(COLOR_BTNFACE));
    ReleaseDC(opacitySlider, sliderDc);
    const auto statusDc = GetDC(settingsStatus);
    REQUIRE(statusDc != nullptr);
    CHECK(reinterpret_cast<HBRUSH>(SendMessageW(
              settings.handle(), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(statusDc),
              reinterpret_cast<LPARAM>(settingsStatus)))
          == GetSysColorBrush(COLOR_BTNFACE));
    CHECK(GetBkMode(statusDc) == TRANSPARENT);
    ReleaseDC(settingsStatus, statusDc);
    CHECK(reinterpret_cast<HBRUSH>(SendMessageW(settings.handle(), WM_CTLCOLORDLG, 0, 0))
          == GetSysColorBrush(COLOR_BTNFACE));
    RECT compactEditBounds{};
    REQUIRE(GetWindowRect(opacityEdit, &compactEditBounds));
    CHECK(compactEditBounds.bottom - compactEditBounds.top
          == MulDiv(22, static_cast<int>(GetDpiForWindow(settings.handle())), 96));
    const auto comboSelectionHeight =
        SendMessageW(hotkeyKey, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
    const auto settingsDpi = static_cast<int>(GetDpiForWindow(settings.handle()));
    RECT settingsClient{};
    REQUIRE(GetClientRect(settings.handle(), &settingsClient));
    CHECK(settingsClient.right - settingsClient.left == MulDiv(560, settingsDpi, 96));
    CHECK(settingsClient.bottom - settingsClient.top == MulDiv(590, settingsDpi, 96));
    RECT screenEdgeGroupBounds{};
    RECT bottomRightZoneBounds{};
    REQUIRE(GetWindowRect(screenEdgeGroup, &screenEdgeGroupBounds));
    REQUIRE(GetWindowRect(bottomRightZone, &bottomRightZoneBounds));
    CHECK(bottomRightZoneBounds.right
          <= screenEdgeGroupBounds.right - MulDiv(12, settingsDpi, 96));
    CHECK(comboSelectionHeight >= MulDiv(16, settingsDpi, 96));
    CHECK(comboSelectionHeight <= MulDiv(20, settingsDpi, 96));
    CHECK(SendMessageW(hotkeyKey, CB_GETCOUNT, 0, 0) == 60);
    CHECK(SendMessageW(
              hotkeyKey, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
              reinterpret_cast<LPARAM>(L"Space"))
          == 0);
    CHECK(SendMessageW(
              hotkeyKey, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
              reinterpret_cast<LPARAM>(L"0"))
          == 1);
    CHECK(SendMessageW(
              hotkeyKey, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
              reinterpret_cast<LPARAM>(L"9"))
          == 10);
    RECT hotkeyDroppedBounds{};
    REQUIRE(SendMessageW(
        hotkeyKey, CB_GETDROPPEDCONTROLRECT, 0,
        reinterpret_cast<LPARAM>(&hotkeyDroppedBounds)));
    CHECK(hotkeyDroppedBounds.bottom - hotkeyDroppedBounds.top
          >= comboSelectionHeight * 11);
    SendMessageW(hotkeyKey, CB_SETTOPINDEX, 12, 0);
    SendMessageW(
        settings.handle(), WM_COMMAND, MAKEWPARAM(2015, CBN_DROPDOWN),
        reinterpret_cast<LPARAM>(hotkeyKey));
    CHECK(SendMessageW(hotkeyKey, CB_GETTOPINDEX, 0, 0) == 0);
    CHECK(SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0) == 2);
    CHECK(SendMessageW(backdropCombo, CB_GETCOUNT, 0, 0) == 4);
    const auto initialBackdrop = SendMessageW(backdropCombo, CB_GETCURSEL, 0, 0);
    REQUIRE(initialBackdrop != CB_ERR);
    CHECK(static_cast<hlaunch::core::BackdropMode>(
              SendMessageW(backdropCombo, CB_GETITEMDATA, initialBackdrop, 0))
          == hlaunch::core::BackdropMode::Acrylic);
    const auto controlsFitClient = [&settings] {
        RECT client{};
        GetClientRect(settings.handle(), &client);
        struct Context {
            RECT client;
            bool fits{true};
            int firstOutId{};
            RECT firstOutBounds{};
        } context{client};
        EnumChildWindows(
            settings.handle(),
            [](const HWND child, const LPARAM value) noexcept -> BOOL {
                auto& context = *reinterpret_cast<Context*>(value);
                if (!IsWindowVisible(child)) return TRUE;
                RECT bounds{};
                GetWindowRect(child, &bounds);
                MapWindowPoints(HWND_DESKTOP, GetParent(child),
                                reinterpret_cast<POINT*>(&bounds), 2);
                const bool fits = bounds.left >= context.client.left
                    && bounds.top >= context.client.top && bounds.right <= context.client.right
                    && bounds.bottom <= context.client.bottom;
                if (!fits && context.fits) {
                    context.firstOutId = GetDlgCtrlID(child);
                    context.firstOutBounds = bounds;
                }
                context.fits = context.fits && fits;
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&context));
        return context;
    };
    auto fit = controlsFitClient();
    INFO(fit.firstOutId);
    INFO(fit.firstOutBounds.left);
    INFO(fit.firstOutBounds.top);
    INFO(fit.firstOutBounds.right);
    INFO(fit.firstOutBounds.bottom);
    CHECK(fit.fits);
    SendMessageW(tab, TCM_SETCURSEL, 1, 0);
    NMHDR selectedTab{.hwndFrom = tab, .idFrom = 2000, .code = TCN_SELCHANGE};
    SendMessageW(settings.handle(), WM_NOTIFY, 2000,
                 reinterpret_cast<LPARAM>(&selectedTab));
    CHECK(IsWindowVisible(hotkeyKey));
    SendMessageW(hotkeyKey, CB_SHOWDROPDOWN, TRUE, 0);
    CHECK(SendMessageW(hotkeyKey, CB_GETDROPPEDSTATE, 0, 0) != FALSE);
    COMBOBOXINFO hotkeyComboInfo{.cbSize = sizeof(COMBOBOXINFO)};
    REQUIRE(GetComboBoxInfo(hotkeyKey, &hotkeyComboInfo));
    REQUIRE(hotkeyComboInfo.hwndList != nullptr);
    CHECK((GetWindowLongPtrW(hotkeyComboInfo.hwndList, GWL_STYLE) & WS_VSCROLL) != 0);
    SendMessageW(hotkeyKey, CB_SETTOPINDEX, 0, 0);
    SendMessageW(
        hotkeyComboInfo.hwndList, WM_MOUSEWHEEL,
        MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    CHECK(SendMessageW(hotkeyKey, CB_GETTOPINDEX, 0, 0) > 0);
    SendMessageW(hotkeyKey, CB_SETTOPINDEX, 20, 0);
    CHECK(SendMessageW(hotkeyKey, CB_GETTOPINDEX, 0, 0) == 20);
    SendMessageW(
        settings.handle(), WM_COMMAND, MAKEWPARAM(2015, CBN_DROPDOWN),
        reinterpret_cast<LPARAM>(hotkeyKey));
    CHECK(SendMessageW(hotkeyKey, CB_GETTOPINDEX, 0, 0) == 0);
    SendMessageW(hotkeyKey, CB_SHOWDROPDOWN, FALSE, 0);
    CHECK_FALSE(IsWindowVisible(backdropCombo));
    CHECK_FALSE(IsWindowVisible(opacityEdit));
    const auto processBlocklistStyle = GetWindowLongPtrW(processBlocklist, GWL_STYLE);
    const auto processAllowlistStyle = GetWindowLongPtrW(processAllowlist, GWL_STYLE);
    CHECK((processBlocklistStyle & ES_MULTILINE) != 0);
    CHECK((processBlocklistStyle & ES_AUTOVSCROLL) != 0);
    CHECK((processBlocklistStyle & ES_WANTRETURN) != 0);
    CHECK((processBlocklistStyle & WS_VSCROLL) != 0);
    CHECK((processAllowlistStyle & ES_MULTILINE) != 0);
    CHECK((processAllowlistStyle & WS_VSCROLL) != 0);
    RECT processBlocklistBounds{};
    REQUIRE(GetWindowRect(processBlocklist, &processBlocklistBounds));
    CHECK(processBlocklistBounds.bottom - processBlocklistBounds.top
          == MulDiv(58, settingsDpi, 96));
    fit = controlsFitClient();
    INFO(fit.firstOutId);
    INFO(fit.firstOutBounds.left);
    INFO(fit.firstOutBounds.top);
    INFO(fit.firstOutBounds.right);
    INFO(fit.firstOutBounds.bottom);
    CHECK(fit.fits);

    SetWindowTextW(opacityEdit, L"29");
    SendMessageW(settings.handle(), WM_COMMAND, MAKEWPARAM(2003, BN_CLICKED), 0);
    CHECK(selectedAppearance.opacityPercent == 95);
    CHECK(selectedDiagnostics);
    SetWindowTextW(opacityEdit, L"72");
    const auto mica = SendMessageW(backdropCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                   reinterpret_cast<LPARAM>(L"云母（Mica）"));
    REQUIRE(mica != CB_ERR);
    SendMessageW(backdropCombo, CB_SETCURSEL, mica, 0);
    CheckDlgButton(settings.handle(), 2011, BST_UNCHECKED);
    CheckDlgButton(settings.handle(), 2012, BST_CHECKED);
    const auto keyB = SendMessageW(hotkeyKey, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                   reinterpret_cast<LPARAM>(L"B"));
    REQUIRE(keyB != CB_ERR);
    SendMessageW(hotkeyKey, CB_SETCURSEL, keyB, 0);
    SetWindowTextW(processBlocklist, L"Game\r\nMSTSC.exe; game.exe");
    SetWindowTextW(processAllowlist, L"mstsc\r\nexplorer.exe");
    CheckDlgButton(settings.handle(), 2050, BST_UNCHECKED);
    SendMessageW(settings.handle(), WM_COMMAND, MAKEWPARAM(2003, BN_CLICKED), 0);
    CHECK(selectedAppearance.backdrop == hlaunch::core::BackdropMode::Mica);
    CHECK(selectedAppearance.opacityPercent == 72);
    CHECK(selectedActivation.hotkey.modifiers
          == std::vector{hlaunch::core::HotkeyModifier::Control});
    CHECK(selectedActivation.hotkey.key == "B");
    CHECK(selectedActivation.screenEdge.foregroundProcessBlocklist
          == std::vector<std::string>{"game.exe", "mstsc.exe"});
    CHECK(selectedActivation.screenEdge.foregroundProcessAllowlist
          == std::vector<std::string>{"mstsc.exe", "explorer.exe"});
    CHECK(selectedActivation.screenEdge.thicknessDip
          == doctest::Approx(hlaunch::core::defaultScreenEdgeThicknessDip));
    CHECK(selectedActivation.screenEdge.cornerSizeDip
          == doctest::Approx(hlaunch::core::defaultScreenEdgeCornerSizeDip));
    CHECK(selectedActivation.screenEdge.dwellMs
          == hlaunch::core::defaultScreenEdgeDwellMs);
    CHECK(selectedActivation.screenEdge.pollMs
          == hlaunch::core::defaultScreenEdgePollMs);
    CHECK(selectedActivation.screenEdge.cooldownMs
          == hlaunch::core::defaultScreenEdgeCooldownMs);

    CHECK_FALSE(selectedDiagnostics);

    settings.hide();
    CHECK_FALSE(settings.isVisible());
    REQUIRE(settings.show(
        GetModuleHandleW(nullptr), nullptr, selectedAppearance, selectedActivation,
        selectedDiagnostics,
        [&selectedAppearance](const hlaunch::core::AppearanceConfig& appearance) {
            selectedAppearance = appearance;
            return true;
        },
        [&selectedActivation](const hlaunch::core::ActivationConfig& activation)
            -> std::expected<void, std::wstring> {
            selectedActivation = activation;
            return {};
        },
        [&selectedDiagnostics](const bool enabled) -> std::expected<void, std::wstring> {
            selectedDiagnostics = enabled;
            return {};
        }));
    CHECK(settings.handle() == initialWindow);
    wchar_t formattedBlocklist[128]{};
    GetWindowTextW(
        processBlocklist, formattedBlocklist, static_cast<int>(std::size(formattedBlocklist)));
    CHECK(std::wstring_view{formattedBlocklist} == L"game.exe\r\nmstsc.exe");
    CHECK(SendMessageW(processBlocklist, EM_GETLINECOUNT, 0, 0) == 2);
    settings.hide();
}

TEST_CASE("UI-SYSTEM-001 system appearance uses Windows settings and refresh "
          "messages")
{
    CHECK(hlaunch::ui::LauncherMetrics{}.itemIconSize == doctest::Approx(32.0F));
    CHECK(hlaunch::ui::iconPixelSizeForDpi(32.0F, 96U) == 32U);
    CHECK(hlaunch::ui::iconPixelSizeForDpi(32.0F, 120U) == 40U);
    CHECK(hlaunch::ui::iconPixelSizeForDpi(32.0F, 144U) == 48U);
    CHECK(hlaunch::ui::iconPixelSizeForDpi(32.0F, 168U) == 56U);
    CHECK(hlaunch::ui::iconPixelSizeForDpi(32.0F, 192U) == 64U);
    CHECK_FALSE(hlaunch::ui::systemUiFontFamily(96).empty());
    hlaunch::ui::SystemUiFont font{};
    REQUIRE(font.refresh(96));
    CHECK(font.get() != nullptr);
    CHECK(hlaunch::ui::isSystemAppearanceMessage(WM_THEMECHANGED));
    CHECK(hlaunch::ui::isSystemAppearanceMessage(WM_SETTINGCHANGE));
    CHECK(hlaunch::ui::isSystemAppearanceMessage(WM_SYSCOLORCHANGE));
    CHECK_FALSE(hlaunch::ui::isSystemAppearanceMessage(WM_COMMAND));

    const auto builtIn = hlaunch::ui::launcherPalette(false);
    const auto highContrast = hlaunch::ui::launcherPalette(true);
    CHECK(builtIn.background == 0x373737U);
    CHECK(builtIn.surface == 0x515151U);
    CHECK(builtIn.tabBackground == 0x252525U);
    CHECK(builtIn.tabHover == 0x414141U);
    CHECK(builtIn.background != builtIn.text);
    CHECK(builtIn.tabIndicator == 0x0D7FD9U);
    CHECK(builtIn.inputBackground == 0x303030U);
    CHECK(builtIn.inputBorder == 0x626262U);
    CHECK(builtIn.itemHighlight == 0xFFFFFFU);
    CHECK(builtIn.focus == 0x0D7FD9U);
    CHECK(builtIn.inputBackground != builtIn.text);
    CHECK(highContrast.tabIndicator == highContrast.accent);
    CHECK(highContrast.itemHighlight == highContrast.focus);
    CHECK(highContrast.background == hlaunch::ui::launcherPalette(true).background);
}

TEST_CASE("UI-STYLE-001 item borders stay fully inside the clipped Grid")
{
    const hlaunch::ui::LauncherMetrics metrics{};
    const auto layout = hlaunch::ui::calculateLauncherLayout(
        {394.0F, 590.0F, 1U}, metrics);
    REQUIRE_FALSE(layout.items.empty());
    const auto& firstItem = layout.items.front();
    const auto border = hlaunch::ui::insetRectForInsideStroke(
        firstItem, metrics.itemHoverBorderWidth);
    CHECK(metrics.itemHoverBorderWidth == doctest::Approx(1.0F));
    CHECK(metrics.itemFocusBorderWidth == doctest::Approx(1.0F));
    CHECK(metrics.itemDropBorderWidth == doctest::Approx(3.0F));
    CHECK(border.x == doctest::Approx(firstItem.x + 0.5F));
    CHECK(border.y == doctest::Approx(firstItem.y + 0.5F));
    CHECK(border.width == doctest::Approx(firstItem.width - 1.0F));
    CHECK(border.height == doctest::Approx(firstItem.height - 1.0F));
    CHECK(border.y - 0.5F >= layout.grid.y);
    CHECK(border.x - 0.5F >= layout.grid.x);
}

TEST_CASE("UI-STYLE-001 title buttons use icon-only hover and pinned tones")
{
    using hlaunch::ui::LauncherChromeIcon;
    using hlaunch::ui::LauncherChromeIconTone;
    using hlaunch::ui::launcherChromeIconTone;

    const hlaunch::ui::LauncherMetrics metrics{};
    CHECK(metrics.pinIconSize == doctest::Approx(12.0F));
    CHECK(metrics.pinIconSize < metrics.chromeIconSize);

    CHECK(launcherChromeIconTone(LauncherChromeIcon::Menu, false, false)
          == LauncherChromeIconTone::Muted);
    CHECK(launcherChromeIconTone(LauncherChromeIcon::Menu, true, false)
          == LauncherChromeIconTone::Text);
    CHECK(launcherChromeIconTone(LauncherChromeIcon::Pin, false, true)
          == LauncherChromeIconTone::Accent);
    CHECK(launcherChromeIconTone(LauncherChromeIcon::Pin, true, true)
          == LauncherChromeIconTone::Text);
    CHECK(launcherChromeIconTone(LauncherChromeIcon::Close, false, false)
          == LauncherChromeIconTone::Muted);
    CHECK(launcherChromeIconTone(LauncherChromeIcon::Close, true, false)
          == LauncherChromeIconTone::Danger);
}

TEST_CASE("PLAT-SHELL-001 process exposes stable AppUserModelID")
{
    REQUIRE(hlaunch::platform::windows::setProcessAppUserModelId());
    PWSTR current{};
    REQUIRE(SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&current)));
    REQUIRE(current != nullptr);
    CHECK(std::wstring_view{current} == hlaunch::platform::windows::appUserModelId);
    CoTaskMemFree(current);
}

TEST_CASE("PLAT-SINGLE-001 command line maps activation commands without "
          "payload pointers")
{
    constexpr std::array arguments{
        std::wstring_view{L"--portable"},
        std::wstring_view{L"--show-search"},
        std::wstring_view{L"--toggle"},
    };
    const auto options = hlaunch::app::parseCommandLine(arguments);

    REQUIRE(options.has_value());
    CHECK(options->portable);
    CHECK(options->showSearch);
    REQUIRE(options->activation.has_value());
    CHECK(*options->activation == hlaunch::platform::windows::ActivationCommand::Toggle);
}

TEST_CASE("PLAT-SINGLE-001 command line rejects unknown options")
{
    constexpr std::array arguments{std::wstring_view{L"--unknown"}};
    CHECK_FALSE(hlaunch::app::parseCommandLine(arguments).has_value());
}

TEST_CASE("PLAT-SINGLE-001 secondary instance retries until the primary window is ready")
{
    using hlaunch::platform::windows::ActivationCommand;
    using hlaunch::platform::windows::PrimaryNotificationOptions;
    receivedActivationCommand.store(0U);
    std::atomic_bool windowCreated{};
    std::jthread primaryThread([&](const std::stop_token stopToken) {
        Sleep(40U);
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = activationTestWindowProcedure;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = hlaunch::platform::windows::activationWindowClassName;
        if (!RegisterClassExW(&windowClass)
            && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        const auto window = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            hlaunch::platform::windows::activationWindowClassName,
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
        if (!window) {
            return;
        }
        windowCreated.store(true);
        while (!stopToken.stop_requested() && receivedActivationCommand.load() == 0U) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(1U);
        }
        DestroyWindow(window);
    });

    const auto result = hlaunch::platform::windows::notifyPrimaryInstance(
        ActivationCommand::Toggle,
        PrimaryNotificationOptions{
            .findAttempts = 40U,
            .retryDelayMilliseconds = 5U,
            .sendTimeoutMilliseconds = 500U,
        });
    primaryThread.request_stop();
    primaryThread.join();

    REQUIRE(windowCreated.load());
    REQUIRE(result.has_value());
    CHECK(receivedActivationCommand.load()
          == static_cast<WPARAM>(ActivationCommand::Toggle));
}

TEST_CASE("PLAT-SINGLE-001 secondary instance reports a missing primary window")
{
    using hlaunch::platform::windows::ActivationCommand;
    using hlaunch::platform::windows::PrimaryNotificationErrorCode;
    using hlaunch::platform::windows::PrimaryNotificationOptions;
    const auto result = hlaunch::platform::windows::notifyPrimaryInstance(
        ActivationCommand::Show,
        PrimaryNotificationOptions{
            .findAttempts = 2U,
            .retryDelayMilliseconds = 1U,
            .sendTimeoutMilliseconds = 10U,
        });

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PrimaryNotificationErrorCode::WindowNotFound);
    CHECK(result.error().systemCode == ERROR_FILE_NOT_FOUND);
}

TEST_CASE("UI-EFFECT-001 defaults to Acrylic with 95 percent opacity")
{
    constexpr std::array<std::wstring_view, 0> arguments{};
    const auto options = hlaunch::app::parseCommandLine(arguments);

    REQUIRE(options.has_value());
    CHECK(options->windowEffects.backdrop == hlaunch::platform::windows::WindowBackdrop::Acrylic);
    CHECK(options->windowEffects.opacityPercent == 95);
    CHECK_FALSE(options->backdropSpecified);
    CHECK_FALSE(options->opacitySpecified);
    CHECK_FALSE(options->showSearch);
}

TEST_CASE("UI-EFFECT-001 parses every supported backdrop and window opacity")
{
    using hlaunch::platform::windows::WindowBackdrop;
    constexpr std::array cases{
        std::pair{std::wstring_view{L"--backdrop=solid"}, WindowBackdrop::Solid},
        std::pair{std::wstring_view{L"--backdrop=mica"}, WindowBackdrop::Mica},
        std::pair{std::wstring_view{L"--backdrop=acrylic"}, WindowBackdrop::Acrylic},
        std::pair{std::wstring_view{L"--backdrop=tabbed"}, WindowBackdrop::Tabbed},
    };
    for (const auto& [argument, expected] : cases) {
        const std::array arguments{argument, std::wstring_view{L"--opacity=72"}};
        const auto options = hlaunch::app::parseCommandLine(arguments);
        REQUIRE(options.has_value());
        CHECK(options->windowEffects.backdrop == expected);
        CHECK(options->windowEffects.opacityPercent == 72);
        CHECK(options->backdropSpecified);
        CHECK(options->opacitySpecified);
    }
}

TEST_CASE("UI-EFFECT-001 rejects invisible or malformed opacity and unknown "
          "backdrop")
{
    constexpr std::array tooLow{std::wstring_view{L"--opacity=29"}};
    constexpr std::array tooHigh{std::wstring_view{L"--opacity=101"}};
    constexpr std::array malformed{std::wstring_view{L"--opacity=80%"}};
    constexpr std::array unknownBackdrop{std::wstring_view{L"--backdrop=glass"}};

    CHECK_FALSE(hlaunch::app::parseCommandLine(tooLow).has_value());
    CHECK_FALSE(hlaunch::app::parseCommandLine(tooHigh).has_value());
    CHECK_FALSE(hlaunch::app::parseCommandLine(malformed).has_value());
    CHECK_FALSE(hlaunch::app::parseCommandLine(unknownBackdrop).has_value());
}

TEST_CASE("UI-EFFECT-001 restores a fully opaque window after live changes")
{
    const auto window = CreateWindowExW(0, L"STATIC", L"HLaunch effect test", WS_POPUP, 0, 0, 120,
                                        120, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    REQUIRE(window != nullptr);

    const auto translucent = hlaunch::platform::windows::applyWindowEffects(
        window,
        {.backdrop = hlaunch::platform::windows::WindowBackdrop::Acrylic, .opacityPercent = 95});
    CHECK(translucent.globalOpacityApplied);
    CHECK((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED) != 0);

    static_cast<void>(hlaunch::platform::windows::applyWindowEffects(
        window,
        {.backdrop = hlaunch::platform::windows::WindowBackdrop::Solid, .opacityPercent = 100}));
    CHECK((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED) == 0);
    DestroyWindow(window);
}

TEST_CASE("PROD-GRID-001 launcher layout remains DIP based and responsive")
{
    const auto compact = hlaunch::ui::calculateLauncherLayout({280.0F, 520.0F, 25});
    const auto regular = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 25});
    const auto wide = hlaunch::ui::calculateLauncherLayout({600.0F, 650.0F, 25});

    CHECK(compact.columns == 3);
    CHECK(regular.columns == 5);
    CHECK(wide.columns == 7);
    CHECK(regular.items.size() == 25);
    CHECK(regular.items[0].x == doctest::Approx(22.0F));
    CHECK(regular.items[0].y == doctest::Approx(28.0F));
    CHECK(regular.items[5].y > regular.items[0].y);
    CHECK(regular.tabs.y > regular.grid.y);
    const auto defaultLayout = hlaunch::ui::calculateLauncherLayout({394.0F, 590.0F, 40U});
    REQUIRE(defaultLayout.items.size() == 40U);
    CHECK(defaultLayout.header.height == doctest::Approx(16.0F));
    CHECK(defaultLayout.headerContent.y
          == doctest::Approx(defaultLayout.header.y - 1.0F));
    CHECK(defaultLayout.headerContent.height
          == doctest::Approx(defaultLayout.header.height));
    CHECK(defaultLayout.tabs.height == doctest::Approx(30.0F));
    CHECK(defaultLayout.menuButton.width == doctest::Approx(16.0F));
    CHECK(defaultLayout.closeButton.x
              - (defaultLayout.pinButton.x + defaultLayout.pinButton.width)
          == doctest::Approx(4.0F));
    CHECK(hlaunch::ui::isLauncherDragRegion(
        defaultLayout,
        defaultLayout.pinButton.x + defaultLayout.pinButton.width + 2.0F,
        defaultLayout.pinButton.y + defaultLayout.pinButton.height / 2.0F));
    CHECK(defaultLayout.tabs.y
              - (defaultLayout.items[35].y + defaultLayout.items[35].height)
          == doctest::Approx(8.0F));
    CHECK(regular.tabs.y + regular.tabs.height == doctest::Approx(640.0F));
    CHECK(hlaunch::ui::calculateLauncherGridCapacity(280.0F, 520.0F) == 18);
    CHECK(hlaunch::ui::calculateLauncherGridCapacity(420.0F, 640.0F) == 40);
    CHECK(hlaunch::ui::calculateLauncherGridCapacity(600.0F, 650.0F) == 56);
}

TEST_CASE("PROD-GRID-001 window resizing quantizes to complete rows and columns")
{
    using hlaunch::ui::LauncherGridSize;
    using hlaunch::ui::LauncherResizeEdge;
    using hlaunch::ui::RectPixels;

    CHECK(hlaunch::ui::calculateLauncherWindowSizeDip({5U, 8U}) ==
          hlaunch::ui::SizeDip{394.0F, 590.0F});
    CHECK(hlaunch::ui::calculateLauncherWindowSizeDip({6U, 9U}) ==
          hlaunch::ui::SizeDip{470.0F, 656.0F});
    CHECK(hlaunch::ui::calculateLauncherGridSize(470.0F, 656.0F) == LauncherGridSize{6U, 9U});

    const auto right = hlaunch::ui::quantizeLauncherSizingRectangle(RectPixels{100, 200, 548, 805},
                                                                    LauncherResizeEdge::Right, 96);
    CHECK(right == RectPixels{100, 200, 570, 805});

    const auto topLeft = hlaunch::ui::quantizeLauncherSizingRectangle(
        RectPixels{152, 145, 570, 805}, LauncherResizeEdge::TopLeft, 96);
    CHECK(topLeft == RectPixels{176, 149, 570, 805});

    const auto highDpi = hlaunch::ui::quantizeLauncherSizingRectangle(
        RectPixels{0, 0, 705, 990}, LauncherResizeEdge::BottomRight, 144);
    CHECK(highDpi == RectPixels{0, 0, 705, 984});
}

TEST_CASE("UI-STYLE-001 custom Grid and Tab metrics preserve quantized layout")
{
    hlaunch::ui::LauncherMetrics metrics{};
    CHECK(metrics.tabHeight == doctest::Approx(30.0F));
    CHECK(metrics.tabCornerRadius == doctest::Approx(0.0F));
    CHECK(metrics.tabHorizontalInset == doctest::Approx(0.0F));
    CHECK(metrics.tabUnderlineThickness == doctest::Approx(4.0F));
    metrics.itemWidth = 80.0F;
    metrics.itemHeight = 70.0F;
    metrics.itemGap = 6.0F;
    metrics.tabHeight = 38.0F;

    const auto size = hlaunch::ui::calculateLauncherWindowSizeDip({5U, 8U}, metrics);
    CHECK(size == hlaunch::ui::SizeDip{442.0F, 676.0F});

    const auto layout = hlaunch::ui::calculateLauncherLayout({size.width, size.height, 40}, metrics);
    CHECK(layout.columns == 5);
    REQUIRE(layout.items.size() == 40);
    CHECK(layout.items.front().width == doctest::Approx(80.0F));
    CHECK(layout.items.front().height == doctest::Approx(70.0F));
    CHECK(layout.tabs.height == doctest::Approx(38.0F));
    CHECK(hlaunch::ui::calculateLauncherGridSize(size.width, size.height, metrics) ==
          hlaunch::ui::LauncherGridSize{5U, 8U});
}

TEST_CASE("PROD-GRID-001 native edge resize persists grid size and reflows slots")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items =
                {
                    {.id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", .name = "A", .gridSlot = 0U},
                    {.id = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", .name = "B", .gridSlot = 5U},
                },
        }},
    };
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr), {}, false, std::move(document),
        [](const hlaunch::core::LaunchItem&) {}, {}, {}, {}, 5, 8));

    std::uint16_t savedColumns{};
    std::uint16_t savedRows{};
    hlaunch::core::ItemsDocument savedDocument{};
    launcher.setGridSizeChangedHandler([&](const std::uint16_t columns, const std::uint16_t rows) {
        savedColumns = columns;
        savedRows = rows;
        return true;
    });
    launcher.setDocumentChangedHandler(
        [&](const hlaunch::core::ItemsDocument& updated) { savedDocument = updated; });

    RECT original{};
    REQUIRE(GetWindowRect(launcher.handle(), &original));
    const auto middleY = original.top + ((original.bottom - original.top) / 2);
    CHECK(SendMessageW(launcher.handle(), WM_NCHITTEST, 0,
                       MAKELPARAM(original.left + 1, middleY)) == HTLEFT);

    SendMessageW(launcher.handle(), WM_ENTERSIZEMOVE, 0, 0);
    RECT proposed = original;
    proposed.right += MulDiv(76, static_cast<int>(GetDpiForWindow(launcher.handle())), 96);
    REQUIRE(SendMessageW(launcher.handle(), WM_SIZING, WMSZ_RIGHT,
                         reinterpret_cast<LPARAM>(&proposed)) == TRUE);
    REQUIRE(SetWindowPos(launcher.handle(), nullptr, proposed.left, proposed.top,
                         proposed.right - proposed.left, proposed.bottom - proposed.top,
                         SWP_NOACTIVATE | SWP_NOZORDER));
    SendMessageW(launcher.handle(), WM_EXITSIZEMOVE, 0, 0);

    CHECK(savedColumns == 6);
    CHECK(savedRows == 8);
    REQUIRE(savedDocument.tabs.size() == 1U);
    REQUIRE(savedDocument.tabs.front().items.size() == 2U);
    CHECK(savedDocument.tabs.front().items[0].gridSlot == 0U);
    CHECK(savedDocument.tabs.front().items[1].gridSlot == 6U);
    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 shrinking columns previews final item positions "
          "before mouse release")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items =
                {
                    {
                        .id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                        .name = "Right-side item",
                        .target = "item.exe",
                        .gridSlot = 8U,
                    },
                },
        }},
    };
    std::string launchedId{};
    std::size_t documentChangeCount{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(GetModuleHandleW(nullptr), {}, false, std::move(document),
                            [&](const hlaunch::core::LaunchItem& item) { launchedId = item.id; }));
    launcher.setGridSizeChangedHandler(
        [](const std::uint16_t, const std::uint16_t) { return true; });
    launcher.setDocumentChangedHandler(
        [&](const hlaunch::core::ItemsDocument&) { ++documentChangeCount; });

    RECT original{};
    REQUIRE(GetWindowRect(launcher.handle(), &original));
    SendMessageW(launcher.handle(), WM_ENTERSIZEMOVE, 0, 0);
    RECT proposed = original;
    proposed.right -= MulDiv(76, static_cast<int>(GetDpiForWindow(launcher.handle())), 96);
    REQUIRE(SendMessageW(launcher.handle(), WM_SIZING, WMSZ_RIGHT,
                         reinterpret_cast<LPARAM>(&proposed)) == TRUE);
    REQUIRE(SetWindowPos(launcher.handle(), nullptr, proposed.left, proposed.top,
                         proposed.right - proposed.left, proposed.bottom - proposed.top,
                         SWP_NOACTIVATE | SWP_NOZORDER));

    const auto previewLayout = launcherLayoutFor(launcher.handle(), 32U);
    REQUIRE(previewLayout.columns == 4U);
    const auto previewPoint = centerInPixels(launcher.handle(), previewLayout.items[7]);
    SendMessageW(launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(previewPoint.x, previewPoint.y));
    SendMessageW(launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(previewPoint.x, previewPoint.y));
    CHECK(launchedId == "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    CHECK(documentChangeCount == 0U);

    SendMessageW(launcher.handle(), WM_EXITSIZEMOVE, 0, 0);
    CHECK(documentChangeCount == 1U);
    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 detached search aligns without changing the launcher Grid")
{
    const auto launcher = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 25});
    const auto search = hlaunch::ui::calculateSearchPopupLayout(launcher);

    CHECK(launcher.items.front().y == doctest::Approx(28.0F));
    CHECK(search.xOffsetDip == doctest::Approx(0.0F));
    CHECK(search.windowWidthDip == doctest::Approx(420.0F));
    CHECK(search.windowHeightDip == doctest::Approx(48.0F));
    CHECK(search.field.x == doctest::Approx(8.0F));
    CHECK(search.field.y == doctest::Approx(6.0F));
    CHECK(search.field.width == doctest::Approx(404.0F));
    CHECK(search.field.height == doctest::Approx(36.0F));
    CHECK(search.edit.x == doctest::Approx(46.0F));
    CHECK(search.edit.y == doctest::Approx(14.0F));
    CHECK(search.edit.width == doctest::Approx(354.0F));
    CHECK(search.edit.height == doctest::Approx(20.0F));
    CHECK(search.edit.y - search.field.y
          == doctest::Approx(
              (search.field.y + search.field.height)
              - (search.edit.y + search.edit.height)));
}

TEST_CASE("PROD-GRID-001 close gesture hides launcher without terminating its "
          "process")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "默认",
        }},
    };
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(GetModuleHandleW(nullptr),
                            hlaunch::platform::windows::WindowEffects{
                                .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
                            },
                            false, std::move(document), [](const hlaunch::core::LaunchItem&) {}));
    launcher.show();
    REQUIRE(launcher.isVisible());
    SendMessageW(launcher.handle(), WM_CLOSE, 0, 0);
    CHECK_FALSE(launcher.isVisible());
    CHECK(launcher.handle() != nullptr);
}

TEST_CASE("PROD-GRID-001 Alt F4 explicitly terminates the launcher window")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "默认",
        }},
    };
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(GetModuleHandleW(nullptr),
                            hlaunch::platform::windows::WindowEffects{
                                .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
                            },
                            false, std::move(document), [](const hlaunch::core::LaunchItem&) {}));
    const auto window = launcher.handle();
    REQUIRE(window != nullptr);
    SendMessageW(window, WM_SYSKEYDOWN, VK_F4, 0);
    CHECK(launcher.handle() == nullptr);
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 search selects the first result and arrow keys navigate items")
{
    hlaunch::core::ItemsDocument document{
        .tabs =
            {
                hlaunch::core::Tab{
                    .id = "11111111-1111-4111-8111-111111111111",
                    .name = "Common",
                    .items = {searchableItem("11111111-1111-4111-8111-111111111101", "Alphabet")},
                },
                hlaunch::core::Tab{
                    .id = "22222222-2222-4222-8222-222222222222",
                    .name = "Development",
                    .items = {searchableItem("22222222-2222-4222-8222-222222222201", "Alpha")},
                },
            },
    };
    std::string launchedId{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) { launchedId = item.id; }));

    SendMessageW(launcher.handle(), WM_CHAR, L'a', 0);
    const auto search = FindWindowW(L"HLaunch.SearchWindow.v1", L"HLaunch Search");
    REQUIRE(search != nullptr);
    for (const wchar_t character : std::wstring_view{L"lpha"}) {
        SendMessageW(search, WM_CHAR, character, 0);
    }
    SendMessageW(search, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == "22222222-2222-4222-8222-222222222201");

    launchedId.clear();
    SendMessageW(search, WM_KEYDOWN, VK_RIGHT, 0);
    SendMessageW(search, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == "11111111-1111-4111-8111-111111111101");
}

TEST_CASE("PROD-GRID-001 first search character leaves the native caret at the end")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "Common",
        }},
    };
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document), [](const hlaunch::core::LaunchItem&) {}));

    SendMessageW(launcher.handle(), WM_CHAR, L'f', 0);
    const auto search = FindWindowW(L"HLaunch.SearchWindow.v1", L"HLaunch Search");
    REQUIRE(search != nullptr);
    const auto edit = GetDlgItem(search, 4101);
    REQUIRE(edit != nullptr);
    if (!hlaunch::ui::isHighContrastEnabled()) {
        CHECK((GetWindowLongPtrW(edit, GWL_EXSTYLE) & WS_EX_TRANSPARENT) == 0);
        const auto dc = GetDC(edit);
        REQUIRE(dc != nullptr);
        const auto background = SendMessageW(
            search, WM_CTLCOLOREDIT,
            reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(edit));
        REQUIRE(background != 0);
        LOGBRUSH brush{};
        REQUIRE(GetObjectW(
                    reinterpret_cast<HBRUSH>(background), sizeof(brush), &brush)
                == sizeof(brush));
        CHECK(brush.lbColor == RGB(0x30, 0x30, 0x30));
        CHECK(GetBkColor(dc) == RGB(0x30, 0x30, 0x30));
        CHECK(GetBkMode(dc) == OPAQUE);
        ReleaseDC(edit, dc);
    }
    wchar_t first[16]{};
    GetWindowTextW(edit, first, static_cast<int>(std::size(first)));
    CHECK(std::wstring_view{first} == L"f");
    DWORD selectionStart{};
    DWORD selectionEnd{};
    SendMessageW(
        edit, EM_GETSEL,
        reinterpret_cast<WPARAM>(&selectionStart),
        reinterpret_cast<LPARAM>(&selectionEnd));
    CHECK(selectionStart == 1U);
    CHECK(selectionEnd == 1U);

    for (int index = 0; index < 4; ++index) {
        SendMessageW(edit, WM_CHAR, L'd', 0);
    }
    wchar_t completed[16]{};
    GetWindowTextW(edit, completed, static_cast<int>(std::size(completed)));
    CHECK(std::wstring_view{completed} == L"fdddd");
    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 selection starts empty and mouse wheel switches tabs")
{
    hlaunch::core::ItemsDocument document{
        .tabs =
            {
                hlaunch::core::Tab{
                    .id = "11111111-1111-4111-8111-111111111111",
                    .name = "First tab",
                    .items = {searchableItem("first-item", "First item")},
                },
                hlaunch::core::Tab{
                    .id = "22222222-2222-4222-8222-222222222222",
                    .name = "Second tab",
                    .items = {searchableItem("second-item", "Second item")},
                },
            },
    };
    std::string launchedId{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) { launchedId = item.id; }));

    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId.empty());
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == "first-item");

    launchedId.clear();
    SendMessageW(launcher.handle(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
                 0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId.empty());
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == "second-item");
}

TEST_CASE("PROD-GRID-001 overflowing tabs stay windowed while wheel and Tab cycle all pages")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{};
    for (std::size_t index = 0; index < 8U; ++index) {
        const auto suffix = std::to_string(index);
        document.tabs.push_back(hlaunch::core::Tab{
            .id = "tab-" + suffix,
            .name = "Tab " + suffix,
            .items = {searchableItem("item-" + suffix, "Item " + suffix)},
        });
    }

    std::string launchedId{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) { launchedId = item.id; }));

    CHECK((GetWindowLongPtrW(launcher.handle(), GWL_STYLE) & WS_HSCROLL) == 0);
    const auto layout = launcherLayoutFor(launcher.handle(), 1U);
    const auto viewport = hlaunch::ui::calculateLauncherTabViewport(layout, 8U, 0U);
    REQUIRE(viewport.visibleCount < 8U);

    for (std::size_t step = 0; step < viewport.visibleCount; ++step) {
        SendMessageW(
            launcher.handle(), WM_MOUSEWHEEL,
            MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    }
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == "item-" + std::to_string(viewport.visibleCount));

    const auto shiftedViewport = hlaunch::ui::calculateLauncherTabViewport(
        layout, 8U, 1U);
    const auto firstVisibleTab = centerInPixels(
        launcher.handle(),
        hlaunch::ui::calculateLauncherTabRect(
            layout, 8U, shiftedViewport.firstIndex, shiftedViewport.firstIndex));
    SendMessageW(
        launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(firstVisibleTab.x, firstVisibleTab.y));
    SendMessageW(
        launcher.handle(), WM_LBUTTONUP, 0,
        MAKELPARAM(firstVisibleTab.x, firstVisibleTab.y));

    for (std::size_t step = 0; step < 8U - shiftedViewport.firstIndex; ++step) {
        SendMessageW(launcher.handle(), WM_KEYDOWN, VK_TAB, 0);
    }
    launchedId.clear();
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == "item-0");

    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 PageDown keeps the keyboard-selected item visible")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "Many",
        }},
    };
    for (int index = 0; index < 55; ++index) {
        document.tabs[0].items.push_back(
            searchableItem("item-" + std::to_string(index), "Item " + std::to_string(index)));
    }

    std::string launchedId{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) { launchedId = item.id; }));

    CHECK((GetWindowLongPtrW(launcher.handle(), GWL_STYLE) & WS_VSCROLL) == 0);

    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == "item-0");

    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_NEXT, 0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == "item-40");
}

TEST_CASE("PROD-GRID-001 search results remain available beyond the first page")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "Many",
        }},
    };
    for (int index = 0; index < 55; ++index) {
        const auto suffix = index < 10 ? "0" + std::to_string(index) : std::to_string(index);
        document.tabs[0].items.push_back(searchableItem("item-" + suffix, "Match " + suffix));
    }

    std::string launchedId{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) { launchedId = item.id; }));

    SendMessageW(launcher.handle(), WM_CHAR, L'm', 0);
    const auto search = FindWindowW(L"HLaunch.SearchWindow.v1", L"HLaunch Search");
    REQUIRE(search != nullptr);
    for (const wchar_t character : std::wstring_view{L"atch"}) {
        SendMessageW(search, WM_CHAR, character, 0);
    }
    SendMessageW(search, WM_KEYDOWN, VK_NEXT, 0);
    SendMessageW(search, WM_KEYDOWN, VK_RETURN, 0);

    CHECK(launchedId == "item-40");
}

TEST_CASE("PROD-ITEM-001 launcher adds and moves an item while preserving identity")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs =
            {
                hlaunch::core::Tab{
                    .id = "11111111-1111-4111-8111-111111111111",
                    .name = "Common",
                },
                hlaunch::core::Tab{
                    .id = "22222222-2222-4222-8222-222222222222",
                    .name = "Work",
                },
            },
    };
    hlaunch::core::ItemsDocument latest{};
    std::string firstId{};
    std::string launchedId{};
    std::uint32_t changeCount{};
    std::uint32_t editorCallCount{};

    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) { launchedId = item.id; },
        [&](const hlaunch::core::ItemsDocument& changed) {
            latest = changed;
            if (changeCount++ == 0U) {
                firstId = changed.tabs[1].items.front().id;
            }
        }));
    launcher.setItemEditorHandler([&](HWND owner, const std::vector<hlaunch::core::Tab>& tabs,
                                      const std::size_t initialTabIndex,
                                      const hlaunch::core::LaunchItem* initial)
                                      -> std::optional<hlaunch::ui::ItemEditorResult> {
        CHECK(owner == launcher.handle());
        CHECK(tabs.size() == 2);
        if (editorCallCount++ == 0U) {
            CHECK(initial == nullptr);
            CHECK(initialTabIndex == 0U);
            return hlaunch::ui::ItemEditorResult{
                .item =
                    hlaunch::core::LaunchItem{
                        .type = hlaunch::core::ItemType::Url,
                        .name = "运行验证条目",
                        .target = "https://example.com",
                        .arguments = {"--profile", "work"},
                        .workingDirectory = "C:\\Windows",
                        .icon = "C:\\Windows\\System32\\shell32.dll",
                        .runAsAdministrator = true,
                    },
                .tabIndex = 1,
            };
        }
        REQUIRE(initial != nullptr);
        CHECK(initialTabIndex == 1U);
        auto edited = *initial;
        edited.name = "已编辑条目";
        edited.target = "https://example.org/final";
        return hlaunch::ui::ItemEditorResult{
            .item = std::move(edited),
            .tabIndex = 0,
        };
    });

    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_INSERT, 0);
    REQUIRE(changeCount == 1U);
    REQUIRE_FALSE(firstId.empty());
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_F2, 0);

    REQUIRE(editorCallCount == 2U);
    REQUIRE(changeCount == 2U);
    REQUIRE(latest.tabs[0].items.size() == 1U);
    CHECK(latest.tabs[1].items.empty());
    const auto& item = latest.tabs[0].items.front();
    CHECK(item.id == firstId);
    CHECK(item.name == "已编辑条目");
    CHECK(item.type == hlaunch::core::ItemType::Url);
    CHECK(item.target == "https://example.org/final");
    CHECK((item.arguments == std::vector<std::string>{"--profile", "work"}));
    CHECK(item.workingDirectory == "C:\\Windows");
    CHECK(item.icon == "C:\\Windows\\System32\\shell32.dll");
    CHECK(item.runAsAdministrator);

    SendMessageW(launcher.handle(), WM_CHAR, L'已', 0);
    const auto search = FindWindowW(L"HLaunch.SearchWindow.v1", L"HLaunch Search");
    REQUIRE(search != nullptr);
    for (const wchar_t character : std::wstring_view{L"编辑条目"}) {
        SendMessageW(search, WM_CHAR, character, 0);
    }
    SendMessageW(search, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(search, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == firstId);
}

TEST_CASE("PROD-ITEM-001 double-clicking an empty tile has no action")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "Common",
            .items = {{
                .id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                .name = "Existing",
                .gridSlot = 2U,
            }},
        }},
    };
    bool editorOpened{};
    bool saved{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr), {}, false, std::move(document),
        [](const hlaunch::core::LaunchItem&) {},
        [&saved](const hlaunch::core::ItemsDocument&) { saved = true; }));
    launcher.setItemEditorHandler(
        [&editorOpened](HWND, const std::vector<hlaunch::core::Tab>&, std::size_t,
                        const hlaunch::core::LaunchItem*)
            -> std::optional<hlaunch::ui::ItemEditorResult> {
            editorOpened = true;
            return std::nullopt;
        });
    launcher.show();

    constexpr std::size_t requestedSlot = 17U;
    const auto layout = launcherLayoutFor(launcher.handle(), 40U);
    const auto target = centerInPixels(launcher.handle(), layout.items[requestedSlot]);
    SendMessageW(launcher.handle(), WM_LBUTTONDBLCLK, MK_LBUTTON,
                 MAKELPARAM(target.x, target.y));

    CHECK_FALSE(editorOpened);
    CHECK_FALSE(saved);

    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-ITEM-001 Delete removes the focused item and keeps adjacent focus")
{
    clearPendingQuitMessages();
    constexpr std::string_view firstId = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    constexpr std::string_view secondId = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "Common",
            .items =
                {
                    searchableItem(std::string{firstId}, "First"),
                    searchableItem(std::string{secondId}, "Second"),
                },
        }},
    };
    hlaunch::core::ItemsDocument latest{};
    std::string launchedId{};
    std::string confirmedId{};
    std::uint32_t changeCount{};

    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) { launchedId = item.id; },
        [&](const hlaunch::core::ItemsDocument& changed) {
            latest = changed;
            ++changeCount;
        }));
    launcher.setDeleteConfirmationHandler([&](HWND owner, const hlaunch::core::LaunchItem& item) {
        CHECK(owner == launcher.handle());
        confirmedId = item.id;
        return false;
    });
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DELETE, 0);
    CHECK(confirmedId.empty());
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DELETE, 0);
    CHECK(confirmedId == firstId);
    CHECK(changeCount == 0U);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == firstId);

    confirmedId.clear();
    launchedId.clear();
    launcher.setDeleteConfirmationHandler([&](HWND owner, const hlaunch::core::LaunchItem& item) {
        CHECK(owner == launcher.handle());
        confirmedId = item.id;
        return true;
    });

    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DELETE, 0);

    CHECK(confirmedId == firstId);
    CHECK(changeCount == 1U);
    REQUIRE(latest.tabs.size() == 1);
    REQUIRE(latest.tabs[0].items.size() == 1);
    CHECK(latest.tabs[0].items[0].id == secondId);

    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == secondId);
}

TEST_CASE("PLAT-SHELL-001 mouse activation resolves relative item paths")
{
    clearPendingQuitMessages();
    const hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "Common",
            .items = {hlaunch::core::LaunchItem{
                .id = "22222222-2222-4222-8222-222222222222",
                .name = "Relative",
                .target = R"(Tools\App.exe)",
            }},
        }},
    };
    std::string launchedTarget{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, document,
        [&](const hlaunch::core::LaunchItem& item) { launchedTarget = item.target; }, {}, {},
        {.executableDirectory = LR"(C:\Portable\HLaunch)"}));

    const auto layout = launcherLayoutFor(launcher.handle(), 1);
    const auto itemPoint = centerInPixels(launcher.handle(), layout.items.front());
    SendMessageW(launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(itemPoint.x, itemPoint.y));
    SendMessageW(launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(itemPoint.x, itemPoint.y));

    CHECK(launchedTarget == R"(C:\Portable\HLaunch\Tools\App.exe)");
    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 mouse drag reorders items and moves them to another tab")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs =
            {
                hlaunch::core::Tab{
                    .id = "11111111-1111-4111-8111-111111111111",
                    .name = "Common",
                    .items =
                        {
                            searchableItem("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "First"),
                            searchableItem("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "Second"),
                            searchableItem("cccccccc-cccc-4ccc-8ccc-cccccccccccc", "Third"),
                        },
                },
                hlaunch::core::Tab{
                    .id = "22222222-2222-4222-8222-222222222222",
                    .name = "Work",
                },
            },
    };
    hlaunch::core::ItemsDocument latest{};
    std::uint32_t changeCount{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document), [](const hlaunch::core::LaunchItem&) {},
        [&](const hlaunch::core::ItemsDocument& changed) {
            latest = changed;
            ++changeCount;
        }));

    const auto layout = launcherLayoutFor(launcher.handle(), 4);
    const auto first = centerInPixels(launcher.handle(), layout.items[0]);
    const auto third = centerInPixels(launcher.handle(), layout.items[2]);
    SendMessageW(launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(first.x, first.y));
    SendMessageW(launcher.handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(third.x, third.y));
    SendMessageW(launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(third.x, third.y));

    REQUIRE(changeCount == 1U);
    REQUIRE(latest.tabs[0].items.size() == 3U);
    CHECK(latest.tabs[0].items[0].name == "Second");
    CHECK(latest.tabs[0].items[1].name == "Third");
    CHECK(latest.tabs[0].items[2].name == "First");

    const auto reorderedLayout = launcherLayoutFor(launcher.handle(), 4);
    const auto movedItem = centerInPixels(launcher.handle(), reorderedLayout.items[2]);
    const auto secondTab = centerInPixels(
        launcher.handle(), hlaunch::ui::RectDip{
                               .x = reorderedLayout.tabs.x + (reorderedLayout.tabs.width / 2.0F),
                               .y = reorderedLayout.tabs.y,
                               .width = reorderedLayout.tabs.width / 2.0F,
                               .height = reorderedLayout.tabs.height,
                           });
    SendMessageW(launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(movedItem.x, movedItem.y));
    SendMessageW(launcher.handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(secondTab.x, secondTab.y));
    SendMessageW(launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(secondTab.x, secondTab.y));

    REQUIRE(changeCount == 2U);
    REQUIRE(latest.tabs[0].items.size() == 2U);
    REQUIRE(latest.tabs[1].items.size() == 1U);
    CHECK(latest.tabs[1].items[0].name == "First");
    CHECK(latest.tabs[1].items[0].id == "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
}

TEST_CASE("PROD-GRID-001 mouse drag reorders tabs in one operation")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs =
            {
                {.id = "11111111-1111-4111-8111-111111111111", .name = "One"},
                {.id = "22222222-2222-4222-8222-222222222222", .name = "Two"},
                {.id = "33333333-3333-4333-8333-333333333333", .name = "Three"},
                {.id = "44444444-4444-4444-8444-444444444444", .name = "Four"},
            },
    };
    hlaunch::core::ItemsDocument latest{};
    std::uint32_t changeCount{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document), [](const hlaunch::core::LaunchItem&) {},
        [&](const hlaunch::core::ItemsDocument& changed) {
            latest = changed;
            ++changeCount;
        }));

    const auto layout = launcherLayoutFor(launcher.handle(), 40);
    const auto tabBounds = [&](const std::size_t index) {
        const auto width = layout.tabs.width / 4.0F;
        return hlaunch::ui::RectDip{
            .x = layout.tabs.x + static_cast<float>(index) * width,
            .y = layout.tabs.y,
            .width = width,
            .height = layout.tabs.height,
        };
    };
    const auto first = centerInPixels(launcher.handle(), tabBounds(0));
    const auto fourth = centerInPixels(launcher.handle(), tabBounds(3));
    SendMessageW(launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(first.x, first.y));
    SendMessageW(launcher.handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(fourth.x, fourth.y));
    SendMessageW(launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(fourth.x, fourth.y));

    REQUIRE(changeCount == 1U);
    REQUIRE(latest.tabs.size() == 4U);
    CHECK(latest.tabs[0].name == "Two");
    CHECK(latest.tabs[1].name == "Three");
    CHECK(latest.tabs[2].name == "Four");
    CHECK(latest.tabs[3].name == "One");

    const auto movedFirst = centerInPixels(launcher.handle(), tabBounds(3));
    const auto second = centerInPixels(launcher.handle(), tabBounds(1));
    SendMessageW(launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(movedFirst.x, movedFirst.y));
    SendMessageW(launcher.handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(second.x, second.y));
    SendMessageW(launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(second.x, second.y));

    REQUIRE(changeCount == 2U);
    CHECK(latest.tabs[0].name == "Two");
    CHECK(latest.tabs[1].name == "One");
    CHECK(latest.tabs[2].name == "Three");
    CHECK(latest.tabs[3].name == "Four");

    const auto click = centerInPixels(launcher.handle(), tabBounds(2));
    SendMessageW(launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(click.x, click.y));
    SendMessageW(launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(click.x, click.y));
    CHECK(changeCount == 2U);

    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 tab drag exposes insertion line and floating preview "
          "geometry")
{
    const auto layout = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 0U});
    const float tabWidth = layout.tabs.width / 4.0F;
    const float tabY = layout.tabs.y + layout.tabs.height / 2.0F;

    const auto moveRight = hlaunch::ui::calculateTabInsertionTarget(
        layout, 4U, 0U, layout.tabs.x + 3.5F * tabWidth, tabY);
    REQUIRE(moveRight);
    CHECK(moveRight->insertionIndex == 4U);
    CHECK(moveRight->targetIndex == 3U);
    CHECK(moveRight->xDip == doctest::Approx(layout.tabs.x + layout.tabs.width));

    const auto moveLeft = hlaunch::ui::calculateTabInsertionTarget(
        layout, 4U, 3U, layout.tabs.x + 1.5F * tabWidth, tabY);
    REQUIRE(moveLeft);
    CHECK(moveLeft->insertionIndex == 1U);
    CHECK(moveLeft->targetIndex == 1U);

    const auto originalPosition = hlaunch::ui::calculateTabInsertionTarget(
        layout, 4U, 2U, layout.tabs.x + 2.5F * tabWidth, tabY);
    REQUIRE(originalPosition);
    CHECK(originalPosition->insertionIndex == 2U);
    CHECK(originalPosition->targetIndex == 2U);

    const auto upperPreview =
        hlaunch::ui::calculateTabDragPreview(layout, 4U, layout.tabs.x + 2.0F * tabWidth, 240.0F);
    const auto lowerPreview =
        hlaunch::ui::calculateTabDragPreview(layout, 4U, layout.tabs.x + 2.0F * tabWidth, 320.0F);
    CHECK(upperPreview.x >= layout.tabs.x);
    CHECK(upperPreview.x + upperPreview.width <= layout.tabs.x + layout.tabs.width);
    CHECK(upperPreview.y + upperPreview.height < layout.tabs.y);
    CHECK(upperPreview.width == doctest::Approx(tabWidth));
    CHECK(lowerPreview.y == doctest::Approx(upperPreview.y + 80.0F));

    const auto bottomPreview = hlaunch::ui::calculateTabDragPreview(
        layout, 4U, layout.tabs.x + 2.0F * tabWidth, layout.tabs.y);
    CHECK(bottomPreview.y == doctest::Approx(layout.tabs.y - bottomPreview.height / 2.0F));

    const auto itemLayout = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 1U});
    const auto itemPreview = hlaunch::ui::calculateItemDragPreview(itemLayout, 180.0F, 260.0F);
    const auto movedItemPreview = hlaunch::ui::calculateItemDragPreview(itemLayout, 205.0F, 305.0F);
    CHECK(movedItemPreview.x == doctest::Approx(itemPreview.x + 25.0F));
    CHECK(movedItemPreview.y == doctest::Approx(itemPreview.y + 45.0F));
    CHECK(itemPreview.width == doctest::Approx(itemLayout.items.front().width));
    CHECK(itemPreview.height == doctest::Approx(itemLayout.items.front().height));
}

TEST_CASE("PROD-GRID-001 drag preview popup can extend beyond the launcher")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs =
            {
                hlaunch::core::Tab{
                    .id = "11111111-1111-4111-8111-111111111111",
                    .name = "Common",
                    .items =
                        {
                            searchableItem("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "First"),
                        },
                },
            },
    };
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(GetModuleHandleW(nullptr),
                            hlaunch::platform::windows::WindowEffects{
                                .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
                            },
                            false, std::move(document), [](const hlaunch::core::LaunchItem&) {}));
    launcher.show();

    const auto layout = launcherLayoutFor(launcher.handle(), 40);
    const auto source = centerInPixels(launcher.handle(), layout.items.front());
    RECT client{};
    REQUIRE(GetClientRect(launcher.handle(), &client));
    const POINT outside{client.right + 80, client.bottom + 80};
    SendMessageW(launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(source.x, source.y));
    SendMessageW(launcher.handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(outside.x, outside.y));

    const auto preview = FindWindowW(L"HLaunch.DragPreviewWindow.v1", L"HLaunch Drag Preview");
    REQUIRE(preview != nullptr);
    CHECK(IsWindowVisible(preview));
    RECT launcherBounds{};
    RECT previewBounds{};
    REQUIRE(GetWindowRect(launcher.handle(), &launcherBounds));
    REQUIRE(GetWindowRect(preview, &previewBounds));
    CHECK(previewBounds.right > launcherBounds.right);
    CHECK(previewBounds.bottom > launcherBounds.bottom);

    RECT suggestedPreviewBounds{120, 140, 264, 264};
    SendMessageW(
        preview,
        WM_DPICHANGED,
        MAKELONG(144, 144),
        reinterpret_cast<LPARAM>(&suggestedPreviewBounds)); // NOLINT(performance-no-int-to-ptr): WM_DPICHANGED test supplies the required RECT pointer.
    RECT changedPreviewBounds{};
    REQUIRE(GetWindowRect(preview, &changedPreviewBounds));
    CHECK(changedPreviewBounds.left == suggestedPreviewBounds.left);
    CHECK(changedPreviewBounds.top == suggestedPreviewBounds.top);
    CHECK(changedPreviewBounds.right == suggestedPreviewBounds.right);
    CHECK(changedPreviewBounds.bottom == suggestedPreviewBounds.bottom);

    SendMessageW(launcher.handle(), WM_LBUTTONUP, 0, MAKELPARAM(outside.x, outside.y));
    CHECK_FALSE(IsWindowVisible(preview));
    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 launcher layout always retains at least one column")
{
    const auto layout = hlaunch::ui::calculateLauncherLayout({80.0F, 240.0F, 2});
    CHECK(layout.columns == 1);
    CHECK(layout.items.size() == 2);
}

TEST_CASE("PROD-GRID-001 launcher hit testing resolves items and tabs")
{
    const auto layout = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 3});

    CHECK(hlaunch::ui::hitTestLauncherItem(layout, layout.items[1].x + 4.0F,
                                           layout.items[1].y + 4.0F) == 1);
    CHECK_FALSE(hlaunch::ui::hitTestLauncherItem(layout, 4.0F, 4.0F).has_value());
    CHECK(hlaunch::ui::hitTestLauncherTab(layout, 4, layout.tabs.x + (layout.tabs.width * 0.625F),
                                          layout.tabs.y + 4.0F) == 2);
    CHECK_FALSE(
        hlaunch::ui::hitTestLauncherTab(layout, 0, layout.tabs.x + 4.0F, layout.tabs.y + 4.0F)
            .has_value());
}

TEST_CASE("PROD-GRID-001 overflowing tabs keep a minimum width and expose a windowed range")
{
    const auto layout = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 0U});
    const auto firstViewport = hlaunch::ui::calculateLauncherTabViewport(layout, 10U, 0U);
    REQUIRE(firstViewport.visibleCount > 0U);
    CHECK(firstViewport.visibleCount < 10U);
    CHECK(firstViewport.itemWidth >= hlaunch::ui::defaultLauncherTabMinimumWidthDip);
    CHECK(firstViewport.firstIndex == 0U);
    CHECK(hlaunch::ui::calculateLauncherTabRect(layout, 10U, 0U).width
          == doctest::Approx(firstViewport.itemWidth));
    CHECK(hlaunch::ui::calculateLauncherTabRect(layout, 10U, firstViewport.visibleCount).width
          == doctest::Approx(0.0F));

    const auto lastViewport = hlaunch::ui::calculateLauncherTabViewport(layout, 10U, 9U);
    REQUIRE(lastViewport.firstIndex > 0U);
    CHECK(lastViewport.firstIndex + lastViewport.visibleCount == 10U);
    CHECK(hlaunch::ui::hitTestLauncherTab(
              layout,
              10U,
              layout.tabs.x + 2.0F,
              layout.tabs.y + layout.tabs.height / 2.0F,
              lastViewport.firstIndex)
          == lastViewport.firstIndex);
    CHECK(hlaunch::ui::calculateLauncherTabRect(
              layout, 10U, lastViewport.firstIndex - 1U, lastViewport.firstIndex)
              .width
          == doctest::Approx(0.0F));
}

TEST_CASE("UIA-001 Grid keyboard navigation respects rows and incomplete final "
          "rows")
{
    using hlaunch::ui::GridNavigationDirection;
    using hlaunch::ui::navigateGridItem;

    CHECK(navigateGridItem(0, 6, 5, GridNavigationDirection::Left) == 0);
    CHECK(navigateGridItem(0, 6, 5, GridNavigationDirection::Right) == 1);
    CHECK(navigateGridItem(1, 6, 5, GridNavigationDirection::Down) == 5);
    CHECK(navigateGridItem(5, 6, 5, GridNavigationDirection::Up) == 0);
    CHECK(navigateGridItem(4, 6, 5, GridNavigationDirection::Right) == 4);
    CHECK(navigateGridItem(0, 6, 5, GridNavigationDirection::Last) == 5);
    CHECK(navigateGridItem(5, 6, 5, GridNavigationDirection::First) == 0);
    CHECK_FALSE(navigateGridItem(0, 0, 5, GridNavigationDirection::Down).has_value());
}

TEST_CASE("UIA-001 Tab keyboard navigation wraps in both directions")
{
    using hlaunch::ui::cycleLauncherTab;

    CHECK(cycleLauncherTab(0, 3, false) == 1);
    CHECK(cycleLauncherTab(2, 3, false) == 0);
    CHECK(cycleLauncherTab(0, 3, true) == 2);
    CHECK(cycleLauncherTab(2, 3, true) == 1);
    CHECK_FALSE(cycleLauncherTab(0, 0, false).has_value());
}

TEST_CASE("UIA-001 launcher exposes invokable items and selectable tabs")
{
    const hlaunch::core::ItemsDocument document{
        .tabs =
            {
                hlaunch::core::Tab{
                    .id = "11111111-1111-4111-8111-111111111111",
                    .name = "默认",
                    .items = {searchableItem("22222222-2222-4222-8222-222222222222", "示例项目")},
                },
                hlaunch::core::Tab{
                    .id = "33333333-3333-4333-8333-333333333333",
                    .name = "工作",
                },
            },
    };
    int launchCount{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(GetModuleHandleW(nullptr), {}, false, document,
                            [&](const hlaunch::core::LaunchItem&) { ++launchCount; }));

    winrt::com_ptr<IUIAutomation> automation{};
    REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(automation.put()))));
    winrt::com_ptr<IUIAutomationElement> root{};
    REQUIRE(SUCCEEDED(automation->ElementFromHandle(launcher.handle(), root.put())));
    BSTR rootName{};
    REQUIRE(SUCCEEDED(root->get_CurrentName(&rootName)));
    CHECK(std::wstring_view{rootName ? rootName : L""} == L"HLaunch");
    SysFreeString(rootName);

    const auto findNamed = [&](const wchar_t* name) {
        VARIANT value{};
        value.vt = VT_BSTR;
        value.bstrVal = SysAllocString(name);
        winrt::com_ptr<IUIAutomationCondition> condition{};
        const auto conditionResult =
            automation->CreatePropertyCondition(UIA_NamePropertyId, value, condition.put());
        VariantClear(&value);
        winrt::com_ptr<IUIAutomationElement> element{};
        if (SUCCEEDED(conditionResult)) {
            static_cast<void>(
                root->FindFirst(TreeScope_Descendants, condition.get(), element.put()));
        }
        return element;
    };

    const auto item = findNamed(L"示例项目");
    REQUIRE(item);
    winrt::com_ptr<IUIAutomationInvokePattern> invoke{};
    REQUIRE(SUCCEEDED(item->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(invoke.put()))));
    REQUIRE(SUCCEEDED(invoke->Invoke()));
    CHECK(launchCount == 1);

    const auto tab = findNamed(L"工作");
    REQUIRE(tab);
    winrt::com_ptr<IUIAutomationSelectionItemPattern> selection{};
    REQUIRE(SUCCEEDED(
        tab->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(selection.put()))));
    REQUIRE(SUCCEEDED(selection->Select()));
    BOOL selected{};
    REQUIRE(SUCCEEDED(selection->get_CurrentIsSelected(&selected)));
    CHECK(selected == TRUE);

    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("PROD-ITEM-001 successful launches persist usage statistics")
{
    const std::string itemId = "22222222-2222-4222-8222-222222222222";
    const hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "默认",
            .items = {searchableItem(itemId, "示例项目")},
        }},
    };
    std::optional<hlaunch::core::ItemsDocument> saved{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr), {}, false, document,
        [](const hlaunch::core::LaunchItem&) {}));
    launcher.setDocumentChangedHandler(
        [&saved](const hlaunch::core::ItemsDocument& changed) { saved = changed; });

    launcher.recordSuccessfulLaunch(itemId);

    REQUIRE(saved.has_value());
    REQUIRE(saved->tabs.size() == 1U);
    REQUIRE(saved->tabs.front().items.size() == 1U);
    CHECK(saved->tabs.front().items.front().launchCount == 1U);
    REQUIRE(saved->tabs.front().items.front().lastLaunchedAt.has_value());
    CHECK(hlaunch::core::isValidUtcTimestamp(
        *saved->tabs.front().items.front().lastLaunchedAt));
    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("UI-DRAG-001 interactive launcher regions never initiate window dragging")
{
    const auto layout = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 25});

    CHECK_FALSE(
        hlaunch::ui::isLauncherDragRegion(
            layout,
            layout.items.front().x + layout.items.front().width / 2.0F,
            layout.items.front().y + layout.items.front().height / 2.0F,
            true));
    CHECK_FALSE(
        hlaunch::ui::isLauncherDragRegion(layout, layout.tabs.x + 8.0F, layout.tabs.y + 8.0F));
    CHECK_FALSE(hlaunch::ui::isLauncherDragRegion(layout, layout.closeButton.x + 8.0F,
                                                  layout.closeButton.y + 8.0F));
}

TEST_CASE("UI-HOVER-001 hover hit testing covers actions and ignores empty slots")
{
    using hlaunch::ui::LauncherHoverRegion;
    const auto layout = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 4});
    const auto center = [](const hlaunch::ui::RectDip& rectangle) {
        return std::pair{
            rectangle.x + rectangle.width / 2.0F,
            rectangle.y + rectangle.height / 2.0F,
        };
    };
    const auto [menuX, menuY] = center(layout.menuButton);
    const auto [tabX, tabY] = center(layout.tabs);
    const auto [itemX, itemY] = center(layout.items[0]);
    const auto [emptyX, emptyY] = center(layout.items[3]);

    CHECK(hlaunch::ui::hitTestLauncherHover(layout, 2, 2, false, menuX, menuY).region ==
          LauncherHoverRegion::Menu);
    CHECK(hlaunch::ui::hitTestLauncherHover(layout, 2, 2, false, tabX, tabY).region ==
          LauncherHoverRegion::Tab);
    CHECK(hlaunch::ui::hitTestLauncherHover(layout, 2, 2, false, itemX, itemY).region ==
          LauncherHoverRegion::Item);
    CHECK(hlaunch::ui::hitTestLauncherHover(layout, 2, 2, false, emptyX, emptyY).region ==
          LauncherHoverRegion::None);
    CHECK(hlaunch::ui::hitTestLauncherHover(layout, 2, 2, true, tabX, tabY).region ==
          LauncherHoverRegion::None);
}

TEST_CASE("UI-HOVER-001 occupied item hover updates title and owns a native tooltip")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "默认",
            .items = {searchableItem(
                "22222222-2222-4222-8222-222222222222", "Visual Studio Code")},
        }},
    };
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false, std::move(document), [](const hlaunch::core::LaunchItem&) {}));
    launcher.show();

    const auto layout = launcherLayoutFor(launcher.handle(), 40U);
    const auto occupied = centerInPixels(launcher.handle(), layout.items[0]);
    SendMessageW(
        launcher.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(occupied.x, occupied.y));
    wchar_t title[128]{};
    GetWindowTextW(launcher.handle(), title, static_cast<int>(std::size(title)));
    CHECK(std::wstring_view{title} == L"Visual Studio Code");

    struct TooltipSearch {
        HWND owner{};
        HWND tooltip{};
    } tooltipSearch{.owner = launcher.handle()};
    EnumWindows(
        [](const HWND candidate, const LPARAM value) noexcept -> BOOL {
            auto& search = *reinterpret_cast<TooltipSearch*>(value);
            wchar_t className[64]{};
            GetClassNameW(candidate, className, static_cast<int>(std::size(className)));
            if (GetWindow(candidate, GW_OWNER) == search.owner
                && CompareStringOrdinal(
                       className, -1, TOOLTIPS_CLASSW, -1, TRUE) == CSTR_EQUAL) {
                search.tooltip = candidate;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&tooltipSearch));
    if (!tooltipSearch.tooltip) {
        tooltipSearch.tooltip = FindWindowExW(
            launcher.handle(), nullptr, TOOLTIPS_CLASSW, nullptr);
    }
    CHECK(tooltipSearch.tooltip != nullptr);
    CHECK(SendMessageW(tooltipSearch.tooltip, TTM_GETTOOLCOUNT, 0, 0) == 1);
    CHECK((GetWindowLongPtrW(tooltipSearch.tooltip, GWL_STYLE) & TTS_BALLOON) == 0);

    SendMessageW(launcher.handle(), WM_TIMER, 2U, 0);
    REQUIRE(IsWindowVisible(tooltipSearch.tooltip));
    RECT tooltipBounds{};
    REQUIRE(GetWindowRect(tooltipSearch.tooltip, &tooltipBounds));
    const auto dpi = GetDpiForWindow(launcher.handle());
    POINT expectedAnchor{
        static_cast<LONG>(std::lround(
            (layout.items[0].x + layout.items[0].width / 2.0F)
            * static_cast<float>(dpi) / 96.0F)),
        static_cast<LONG>(std::lround(
            (layout.items[0].y + layout.items[0].height)
            * static_cast<float>(dpi) / 96.0F)),
    };
    REQUIRE(ClientToScreen(launcher.handle(), &expectedAnchor));
    expectedAnchor.y += MulDiv(4, static_cast<int>(dpi), 96);
    const int tooltipCenterX = tooltipBounds.left
        + (tooltipBounds.right - tooltipBounds.left) / 2;
    CHECK(std::abs(tooltipCenterX - expectedAnchor.x) <= 1);
    CHECK(tooltipBounds.top == expectedAnchor.y);

    const auto empty = centerInPixels(launcher.handle(), layout.items[1]);
    SendMessageW(launcher.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(empty.x, empty.y));
    GetWindowTextW(launcher.handle(), title, static_cast<int>(std::size(title)));
    CHECK(std::wstring_view{title} == L"HLaunch");

    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("UI-DRAG-001 launcher chrome and spacing initiate window dragging")
{
    const auto layout = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 25});

    CHECK(hlaunch::ui::isLauncherDragRegion(layout, 4.0F, 4.0F));
    CHECK(
        hlaunch::ui::isLauncherDragRegion(layout, layout.header.x + 40.0F, layout.header.y + 8.0F));
    CHECK(hlaunch::ui::isLauncherDragRegion(layout, layout.grid.x - 8.0F, layout.grid.y + 40.0F));
    CHECK(hlaunch::ui::isLauncherDragRegion(
        layout,
        layout.items.front().x + layout.items.front().width / 2.0F,
        layout.items.front().y + layout.items.front().height / 2.0F,
        false));
    CHECK(hlaunch::ui::isLauncherDragRegion(
        layout,
        layout.items.front().x + layout.items.front().width + 2.0F,
        layout.items.front().y + layout.items.front().height / 2.0F));
    CHECK(hlaunch::ui::isLauncherDragRegion(layout, layout.grid.x + 8.0F,
                                            layout.grid.y + layout.grid.height + 2.0F));
}

TEST_CASE("UI-DRAG-001 empty Grid cell preserves client interaction while preparing window drag")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "One",
            .items = {{
                .id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                .name = "A",
                .gridSlot = 0U,
            }},
        }},
    };
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr), {}, false, std::move(document),
        [](const hlaunch::core::LaunchItem&) {}));
    launcher.show();

    const auto layout = launcherLayoutFor(launcher.handle(), 40U);
    const auto occupied = centerInPixels(launcher.handle(), layout.items[0]);
    const auto empty = centerInPixels(launcher.handle(), layout.items[1]);
    POINT gap{
        static_cast<LONG>(std::lround(layout.items[0].x + layout.items[0].width + 2.0F)),
        static_cast<LONG>(std::lround(layout.items[0].y + layout.items[0].height / 2.0F)),
    };
    const auto dpi = GetDpiForWindow(launcher.handle());
    gap.x = MulDiv(gap.x, static_cast<int>(dpi), 96);
    gap.y = MulDiv(gap.y, static_cast<int>(dpi), 96);
    auto occupiedScreen = occupied;
    auto emptyScreen = empty;
    auto gapScreen = gap;
    REQUIRE(ClientToScreen(launcher.handle(), &occupiedScreen));
    REQUIRE(ClientToScreen(launcher.handle(), &emptyScreen));
    REQUIRE(ClientToScreen(launcher.handle(), &gapScreen));

    CHECK(SendMessageW(launcher.handle(), WM_NCHITTEST, 0,
                       MAKELPARAM(occupiedScreen.x, occupiedScreen.y)) == HTCLIENT);
    CHECK(SendMessageW(launcher.handle(), WM_NCHITTEST, 0,
                       MAKELPARAM(emptyScreen.x, emptyScreen.y)) == HTCLIENT);
    CHECK(SendMessageW(launcher.handle(), WM_NCHITTEST, 0,
                       MAKELPARAM(gapScreen.x, gapScreen.y)) == HTCAPTION);

    SendMessageW(launcher.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(empty.x, empty.y));
    CHECK(GetCapture() == launcher.handle());
    SendMessageW(launcher.handle(), WM_LBUTTONUP, 0,
                 MAKELPARAM(empty.x, empty.y));
    CHECK(GetCapture() != launcher.handle());

    launcher.close();
    clearPendingQuitMessages();
}

TEST_CASE("ACT-HOTKEY-001 activation follows the cursor and stays in the "
          "target work area")
{
    using hlaunch::ui::RectPixels;

    const auto centered = hlaunch::ui::calculateCursorCenteredWindowRectangle(
        RectPixels{-1920, 0, 0, 1040}, -960, 520, 420, 640);
    const auto nearRightTop = hlaunch::ui::calculateCursorCenteredWindowRectangle(
        RectPixels{-1920, 0, 0, 1040}, -100, 100, 420, 640);
    const auto smallerWorkArea = hlaunch::ui::calculateCursorCenteredWindowRectangle(
        RectPixels{100, 50, 400, 250}, 395, 245, 420, 640);

    CHECK(centered.left == -1170);
    CHECK(centered.top == 200);
    CHECK(centered.right == -750);
    CHECK(centered.bottom == 840);
    CHECK(nearRightTop.left == -420);
    CHECK(nearRightTop.top == 0);
    CHECK(nearRightTop.right == 0);
    CHECK(nearRightTop.bottom == 640);
    CHECK(smallerWorkArea.left == 100);
    CHECK(smallerWorkArea.top == 50);
    CHECK(smallerWorkArea.right == 400);
    CHECK(smallerWorkArea.bottom == 250);
}
