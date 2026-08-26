#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "app/command_line.h"
#include "platform/windows/activation_command.h"
#include "platform/windows/window_effects.h"
#include "ui/launcher_layout.h"
#include "ui/launcher_window.h"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace {

hlaunch::core::LaunchItem searchableItem(std::string id, std::string name)
{
    return hlaunch::core::LaunchItem{
        .id = std::move(id),
        .name = std::move(name),
        .target = "not-used-by-test",
    };
}

} // namespace

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
    CHECK(regular.items[0].x == doctest::Approx(24.0F));
    CHECK(regular.items[0].y == doctest::Approx(56.0F));
    CHECK(regular.items[5].y > regular.items[0].y);
    CHECK(regular.tabs.y > regular.grid.y);
    CHECK(regular.tabs.y + regular.tabs.height == doctest::Approx(640.0F));
    CHECK(hlaunch::ui::calculateLauncherGridCapacity(280.0F, 520.0F) == 12);
    CHECK(hlaunch::ui::calculateLauncherGridCapacity(420.0F, 640.0F) == 25);
    CHECK(hlaunch::ui::calculateLauncherGridCapacity(600.0F, 650.0F) == 42);
}

TEST_CASE("PROD-GRID-001 detached search aligns without changing the launcher Grid")
{
    const auto launcher = hlaunch::ui::calculateLauncherLayout({420.0F, 640.0F, 25});
    const auto search = hlaunch::ui::calculateSearchPopupLayout(launcher);

    CHECK(launcher.items.front().y == doctest::Approx(56.0F));
    CHECK(search.xOffsetDip == doctest::Approx(0.0F));
    CHECK(search.windowWidthDip == doctest::Approx(420.0F));
    CHECK(search.windowHeightDip == doctest::Approx(60.0F));
    CHECK(search.field.x == doctest::Approx(8.0F));
    CHECK(search.field.width == doctest::Approx(404.0F));
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
    for (int index = 0; index < 30; ++index) {
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
    CHECK(launchedId == "item-25");
}

TEST_CASE("PROD-GRID-001 search results remain available beyond the first page")
{
    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "Many",
        }},
    };
    for (int index = 0; index < 30; ++index) {
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

    CHECK(launchedId == "item-25");
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

    CHECK(hlaunch::ui::isLauncherDragRegion(layout, 8.0F, 8.0F));
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
        layout.grid.y + layout.grid.height + 6.0F));
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
