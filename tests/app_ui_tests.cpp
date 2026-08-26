#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "app/command_line.h"
#include "platform/windows/activation_command.h"
#include "platform/windows/window_effects.h"
#include "ui/item_context_menu.h"
#include "ui/launcher_context_menu.h"
#include "ui/launcher_layout.h"
#include "ui/launcher_window.h"
#include "ui/settings_window.h"
#include "ui/theme.h"

#include <Ole2.h>
#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

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
        static_cast<LONG>((rectangle.x + (rectangle.width / 2.0F))
            * static_cast<float>(dpi) / 96.0F),
        static_cast<LONG>((rectangle.y + (rectangle.height / 2.0F))
            * static_cast<float>(dpi) / 96.0F),
    };
}

hlaunch::ui::LauncherLayout launcherLayoutFor(const HWND window, const std::size_t itemCount)
{
    RECT client{};
    GetClientRect(window, &client);
    const auto dpi = GetDpiForWindow(window);
    return hlaunch::ui::calculateLauncherLayout({
        .clientWidthDip = static_cast<float>(client.right) * 96.0F
            / static_cast<float>(dpi),
        .clientHeightDip = static_cast<float>(client.bottom) * 96.0F
            / static_cast<float>(dpi),
        .itemCount = itemCount,
    });
}

} // namespace

TEST_CASE("PROD-ITEM-001 item context menu exposes complete grouped actions")
{
    const hlaunch::core::ItemsDocument document{
        .tabs = {
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
    CHECK(GetMenuItemCount(menu.get()) == 13);
    CHECK(GetMenuItemID(menu.get(), 0) == static_cast<UINT>(hlaunch::ui::ItemContextCommand::Open));
    CHECK(GetMenuItemID(menu.get(), 7) == static_cast<UINT>(hlaunch::ui::ItemContextCommand::Insert));
    CHECK(GetMenuItemID(menu.get(), 9) == static_cast<UINT>(hlaunch::ui::ItemContextCommand::Delete));
    CHECK(GetMenuItemID(menu.get(), 12) == static_cast<UINT>(hlaunch::ui::ItemContextCommand::Properties));
    CHECK((GetMenuState(menu.get(), 1, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) != 0U);
    CHECK((GetMenuState(menu.get(), 10, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) != 0U);

    const auto copyMenu = GetSubMenu(menu.get(), 4);
    REQUIRE(copyMenu != nullptr);
    CHECK(GetMenuItemCount(copyMenu) == 3);
    CHECK((GetMenuState(copyMenu, 0, MF_BYPOSITION) & (MF_DISABLED | MF_GRAYED)) != 0U);

    const auto moveMenu = GetSubMenu(menu.get(), 8);
    REQUIRE(moveMenu != nullptr);
    CHECK(GetMenuItemCount(moveMenu) == 2);
    CHECK((GetMenuState(moveMenu, 0, MF_BYPOSITION) & MF_CHECKED) != 0U);
    CHECK((GetMenuState(moveMenu, 1, MF_BYPOSITION) & MF_CHECKED) == 0U);
}

TEST_CASE("PROD-GRID-001 launcher chrome empty slots and tabs expose separate menus")
{
    const auto launcher = hlaunch::ui::createLauncherContextMenu(true);
    REQUIRE(launcher);
    CHECK(GetMenuItemID(launcher.get(), 0)
          == static_cast<UINT>(hlaunch::ui::LauncherContextCommand::ToggleLock));
    CHECK((GetMenuState(launcher.get(), 0, MF_BYPOSITION) & MF_CHECKED) != 0U);
    CHECK(GetMenuItemID(launcher.get(), 4)
          == static_cast<UINT>(hlaunch::ui::LauncherContextCommand::Settings));

    const auto emptySlot = hlaunch::ui::createEmptySlotContextMenu();
    REQUIRE(emptySlot);
    CHECK(GetMenuItemID(emptySlot.get(), 0)
          == static_cast<UINT>(hlaunch::ui::EmptySlotContextCommand::RegisterItem));
    CHECK((GetMenuState(emptySlot.get(), 1, MF_BYPOSITION)
           & (MF_DISABLED | MF_GRAYED)) != 0U);

    const auto tab = hlaunch::ui::createTabContextMenu(false);
    REQUIRE(tab);
    CHECK(GetMenuItemID(tab.get(), 0)
          == static_cast<UINT>(hlaunch::ui::TabContextCommand::AddPage));
    CHECK((GetMenuState(tab.get(), 1, MF_BYPOSITION)
           & (MF_DISABLED | MF_GRAYED)) != 0U);
}

TEST_CASE("UI-THEME-001 settings reuses one window and applies theme selection")
{
    const auto& dark = hlaunch::ui::paletteFor(hlaunch::core::ThemeMode::Dark);
    const auto& light = hlaunch::ui::paletteFor(hlaunch::core::ThemeMode::Light);
    CHECK(dark.background != light.background);
    CHECK(dark.text != light.text);
    CHECK(dark.accent != light.accent);

    auto selectedTheme = hlaunch::core::ThemeMode::Dark;
    hlaunch::ui::SettingsWindow settings{};
    REQUIRE(settings.show(
        GetModuleHandleW(nullptr),
        nullptr,
        hlaunch::core::ThemeMode::Dark,
        [&selectedTheme](const hlaunch::core::ThemeMode theme) {
            selectedTheme = theme;
            return true;
        }));
    REQUIRE(settings.handle() != nullptr);
    CHECK(settings.isVisible());
    const auto initialWindow = settings.handle();
    const auto themeCombo = GetDlgItem(settings.handle(), 2001);
    REQUIRE(themeCombo != nullptr);
    SendMessageW(themeCombo, CB_SETCURSEL, 1, 0);
    SendMessageW(
        settings.handle(),
        WM_COMMAND,
        MAKEWPARAM(2001, CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(themeCombo));
    CHECK(selectedTheme == hlaunch::core::ThemeMode::Light);
    CHECK(SendMessageW(themeCombo, CB_GETCURSEL, 0, 0) == 1);

    settings.hide();
    CHECK_FALSE(settings.isVisible());
    REQUIRE(settings.show(
        GetModuleHandleW(nullptr),
        nullptr,
        hlaunch::core::ThemeMode::Dark,
        [&selectedTheme](const hlaunch::core::ThemeMode theme) {
            selectedTheme = theme;
            return true;
        }));
    CHECK(settings.handle() == initialWindow);
    settings.hide();
}

TEST_CASE("PLAT-SINGLE-001 command line maps activation commands without payload pointers")
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

TEST_CASE("UI-EFFECT-001 defaults to Acrylic with fully opaque window")
{
    constexpr std::array<std::wstring_view, 0> arguments{};
    const auto options = hlaunch::app::parseCommandLine(arguments);

    REQUIRE(options.has_value());
    CHECK(options->windowEffects.backdrop
        == hlaunch::platform::windows::WindowBackdrop::Acrylic);
    CHECK(options->windowEffects.opacityPercent == 100);
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
    }
}

TEST_CASE("UI-EFFECT-001 rejects invisible or malformed opacity and unknown backdrop")
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
    CHECK(regular.items[0].y == doctest::Approx(36.0F));
    CHECK(regular.items[5].y > regular.items[0].y);
    CHECK(regular.tabs.y > regular.grid.y);
    CHECK(regular.tabs.y + regular.tabs.height == doctest::Approx(640.0F));
    CHECK(hlaunch::ui::calculateLauncherGridCapacity(280.0F, 520.0F) == 18);
    CHECK(hlaunch::ui::calculateLauncherGridCapacity(420.0F, 640.0F) == 40);
    CHECK(hlaunch::ui::calculateLauncherGridCapacity(600.0F, 650.0F) == 56);
}

TEST_CASE("PROD-GRID-001 detached search aligns without changing the launcher Grid")
{
    const auto launcher = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 25});
    const auto search = hlaunch::ui::calculateSearchPopupLayout(launcher);

    CHECK(launcher.items.front().y == doctest::Approx(36.0F));
    CHECK(search.xOffsetDip == doctest::Approx(0.0F));
    CHECK(search.windowWidthDip == doctest::Approx(420.0F));
    CHECK(search.windowHeightDip == doctest::Approx(60.0F));
    CHECK(search.field.x == doctest::Approx(8.0F));
    CHECK(search.field.width == doctest::Approx(404.0F));
}

TEST_CASE("PROD-GRID-001 close gesture hides launcher without terminating its process")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "默认",
        }},
    };
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false,
        std::move(document),
        [](const hlaunch::core::LaunchItem&) {}));
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
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false,
        std::move(document),
        [](const hlaunch::core::LaunchItem&) {}));
    const auto window = launcher.handle();
    REQUIRE(window != nullptr);
    SendMessageW(window, WM_SYSKEYDOWN, VK_F4, 0);
    CHECK(launcher.handle() == nullptr);
    clearPendingQuitMessages();
}

TEST_CASE("PROD-GRID-001 launcher typing searches every tab and Enter invokes the exact match")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {
            hlaunch::core::Tab{
                .id = "11111111-1111-4111-8111-111111111111",
                .name = "Common",
                .items = {searchableItem(
                    "11111111-1111-4111-8111-111111111101",
                    "Alphabet")},
            },
            hlaunch::core::Tab{
                .id = "22222222-2222-4222-8222-222222222222",
                .name = "Development",
                .items = {searchableItem(
                    "22222222-2222-4222-8222-222222222201",
                    "Alpha")},
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
        false,
        std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) {
            launchedId = item.id;
        }));

    SendMessageW(launcher.handle(), WM_CHAR, L'a', 0);
    const auto search = FindWindowW(L"HLaunch.SearchWindow.v1", L"HLaunch Search");
    REQUIRE(search != nullptr);
    for (const wchar_t character : std::wstring_view{L"lpha"}) {
        SendMessageW(search, WM_CHAR, character, 0);
    }
    SendMessageW(search, WM_KEYDOWN, VK_RETURN, 0);

    CHECK(launchedId == "22222222-2222-4222-8222-222222222201");
}

TEST_CASE("PROD-GRID-001 Grid wheel and PageDown keep the focused item visible")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "Many",
        }},
    };
    for (int index = 0; index < 55; ++index) {
        document.tabs[0].items.push_back(searchableItem(
            "item-" + std::to_string(index),
            "Item " + std::to_string(index)));
    }

    std::string launchedId{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false,
        std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) {
            launchedId = item.id;
        }));

    SendMessageW(
        launcher.handle(),
        WM_MOUSEWHEEL,
        MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
        0);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == "item-5");

    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_PRIOR, 0);
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
        const auto suffix = index < 10
            ? "0" + std::to_string(index)
            : std::to_string(index);
        document.tabs[0].items.push_back(searchableItem(
            "item-" + suffix,
            "Match " + suffix));
    }

    std::string launchedId{};
    hlaunch::ui::LauncherWindow launcher{};
    REQUIRE(launcher.create(
        GetModuleHandleW(nullptr),
        hlaunch::platform::windows::WindowEffects{
            .backdrop = hlaunch::platform::windows::WindowBackdrop::Solid,
        },
        false,
        std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) {
            launchedId = item.id;
        }));

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
    launcher.setItemEditorHandler(
        [&](HWND owner,
            const std::vector<hlaunch::core::Tab>& tabs,
            const std::size_t initialTabIndex,
            const hlaunch::core::LaunchItem* initial) -> std::optional<hlaunch::ui::ItemEditorResult> {
            CHECK(owner == launcher.handle());
            CHECK(tabs.size() == 2);
            if (editorCallCount++ == 0U) {
                CHECK(initial == nullptr);
                CHECK(initialTabIndex == 0U);
                return hlaunch::ui::ItemEditorResult{
                    .item = hlaunch::core::LaunchItem{
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
    SendMessageW(search, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == firstId);
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
            .items = {
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
        false,
        std::move(document),
        [&launchedId](const hlaunch::core::LaunchItem& item) { launchedId = item.id; },
        [&](const hlaunch::core::ItemsDocument& changed) {
            latest = changed;
            ++changeCount;
        }));
    launcher.setDeleteConfirmationHandler(
        [&](HWND owner, const hlaunch::core::LaunchItem& item) {
            CHECK(owner == launcher.handle());
            confirmedId = item.id;
            return false;
        });
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_DELETE, 0);
    CHECK(confirmedId == firstId);
    CHECK(changeCount == 0U);
    SendMessageW(launcher.handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(launchedId == firstId);

    confirmedId.clear();
    launchedId.clear();
    launcher.setDeleteConfirmationHandler(
        [&](HWND owner, const hlaunch::core::LaunchItem& item) {
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

TEST_CASE("PROD-GRID-001 mouse drag reorders items and moves them to another tab")
{
    clearPendingQuitMessages();
    hlaunch::core::ItemsDocument document{
        .tabs = {
            hlaunch::core::Tab{
                .id = "11111111-1111-4111-8111-111111111111",
                .name = "Common",
                .items = {
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
        false,
        std::move(document),
        [](const hlaunch::core::LaunchItem&) {},
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
        launcher.handle(),
        hlaunch::ui::RectDip{
            .x = reorderedLayout.tabs.x + (reorderedLayout.tabs.width / 2.0F),
            .y = reorderedLayout.tabs.y,
            .width = reorderedLayout.tabs.width / 2.0F,
            .height = reorderedLayout.tabs.height,
        });
    SendMessageW(
        launcher.handle(),
        WM_LBUTTONDOWN,
        MK_LBUTTON,
        MAKELPARAM(movedItem.x, movedItem.y));
    SendMessageW(
        launcher.handle(),
        WM_MOUSEMOVE,
        MK_LBUTTON,
        MAKELPARAM(secondTab.x, secondTab.y));
    SendMessageW(
        launcher.handle(),
        WM_LBUTTONUP,
        0,
        MAKELPARAM(secondTab.x, secondTab.y));

    REQUIRE(changeCount == 2U);
    REQUIRE(latest.tabs[0].items.size() == 2U);
    REQUIRE(latest.tabs[1].items.size() == 1U);
    CHECK(latest.tabs[1].items[0].name == "First");
    CHECK(latest.tabs[1].items[0].id == "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
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

    CHECK(hlaunch::ui::hitTestLauncherItem(
        layout,
        layout.items[1].x + 4.0F,
        layout.items[1].y + 4.0F) == 1);
    CHECK_FALSE(hlaunch::ui::hitTestLauncherItem(layout, 4.0F, 4.0F).has_value());
    CHECK(hlaunch::ui::hitTestLauncherTab(
        layout,
        4,
        layout.tabs.x + (layout.tabs.width * 0.625F),
        layout.tabs.y + 4.0F) == 2);
    CHECK_FALSE(hlaunch::ui::hitTestLauncherTab(
        layout,
        0,
        layout.tabs.x + 4.0F,
        layout.tabs.y + 4.0F).has_value());
}

TEST_CASE("UIA-001 Grid keyboard navigation respects rows and incomplete final rows")
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

TEST_CASE("UI-DRAG-001 interactive launcher regions never initiate window dragging")
{
    const auto layout = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 25});

    CHECK_FALSE(hlaunch::ui::isLauncherDragRegion(
        layout,
        layout.grid.x + 8.0F,
        layout.grid.y + 8.0F));
    CHECK_FALSE(hlaunch::ui::isLauncherDragRegion(
        layout,
        layout.tabs.x + 8.0F,
        layout.tabs.y + 8.0F));
    CHECK_FALSE(hlaunch::ui::isLauncherDragRegion(
        layout,
        layout.closeButton.x + 8.0F,
        layout.closeButton.y + 8.0F));
}

TEST_CASE("UI-DRAG-001 launcher chrome and spacing initiate window dragging")
{
    const auto layout = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 25});

    CHECK(hlaunch::ui::isLauncherDragRegion(layout, 4.0F, 4.0F));
    CHECK(hlaunch::ui::isLauncherDragRegion(
        layout,
        layout.header.x + 40.0F,
        layout.header.y + 8.0F));
    CHECK(hlaunch::ui::isLauncherDragRegion(
        layout,
        layout.grid.x - 8.0F,
        layout.grid.y + 40.0F));
    CHECK(hlaunch::ui::isLauncherDragRegion(
        layout,
        layout.grid.x + 8.0F,
        layout.grid.y + layout.grid.height + 2.0F));
}

TEST_CASE("ACT-HOTKEY-001 activation centers and constrains the launcher in the target work area")
{
    using hlaunch::ui::RectPixels;

    const auto negativeMonitor = hlaunch::ui::calculateCenteredWindowRectangle(
        RectPixels{-1920, 0, 0, 1040},
        420,
        640);
    const auto smallerWorkArea = hlaunch::ui::calculateCenteredWindowRectangle(
        RectPixels{100, 50, 400, 250},
        420,
        640);

    CHECK(negativeMonitor.left == -1170);
    CHECK(negativeMonitor.top == 200);
    CHECK(negativeMonitor.right == -750);
    CHECK(negativeMonitor.bottom == 840);
    CHECK(smallerWorkArea.left == 100);
    CHECK(smallerWorkArea.top == 50);
    CHECK(smallerWorkArea.right == 400);
    CHECK(smallerWorkArea.bottom == 250);
}
