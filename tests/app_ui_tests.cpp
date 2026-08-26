#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "app/command_line.h"
#include "platform/windows/activation_command.h"
#include "platform/windows/window_effects.h"
#include "ui/launcher_layout.h"

#include <doctest/doctest.h>

#include <array>
#include <string_view>
#include <utility>

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
