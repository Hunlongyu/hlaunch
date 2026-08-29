#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "platform/windows/drop_item_resolver.h"

#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        const auto sequence = nextSequence_.fetch_add(1U);
        LARGE_INTEGER timestamp{};
        QueryPerformanceCounter(&timestamp);
        path_ = std::filesystem::temp_directory_path()
            / (L"HLaunchDropTests-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                + std::to_wstring(timestamp.QuadPart) + L"-" + std::to_wstring(sequence));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error{};
        const auto expectedParent = std::filesystem::temp_directory_path().lexically_normal();
        const auto resolvedParent = path_.parent_path().lexically_normal();
        if (resolvedParent == expectedParent
            && path_.filename().wstring().starts_with(L"HLaunchDropTests-")) {
            std::filesystem::remove_all(path_, error);
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    inline static std::atomic_uint32_t nextSequence_{};
    std::filesystem::path path_{};
};

void createFile(const std::filesystem::path& path)
{
    std::ofstream stream{path, std::ios::binary};
    REQUIRE(stream.good());
    stream.put('x');
    REQUIRE(stream.good());
}

} // namespace

TEST_CASE("PROD-DROP-001 resolves supported sources in their original order")
{
    TemporaryDirectory temporary{};
    const auto folder = temporary.path() / L"Folder";
    const auto executable = temporary.path() / L"Tool.EXE";
    const auto shortcut = temporary.path() / L"Shortcut.LnK";
    const auto file = temporary.path() / L"notes.txt";
    REQUIRE(std::filesystem::create_directory(folder));
    createFile(executable);
    createFile(shortcut);
    createFile(file);

    using hlaunch::platform::windows::DroppedSource;
    using hlaunch::platform::windows::DroppedSourceKind;
    const auto result = hlaunch::platform::windows::resolveDroppedSources({
        .targetTabIndex = 2,
        .targetGridSlot = 17,
        .sources = {
            DroppedSource{DroppedSourceKind::Path, folder.wstring()},
            DroppedSource{DroppedSourceKind::Path, executable.wstring()},
            DroppedSource{DroppedSourceKind::Path, shortcut.wstring()},
            DroppedSource{DroppedSourceKind::Path, file.wstring()},
            DroppedSource{DroppedSourceKind::Url, L"https://example.com/path"},
            DroppedSource{DroppedSourceKind::Path, (temporary.path() / L"missing.exe").wstring()},
            DroppedSource{DroppedSourceKind::Url, L"not a URL"},
        },
    });

    CHECK(result.targetTabIndex == 2U);
    CHECK(result.targetGridSlot == 17U);
    CHECK(result.unsupportedCount == 2U);
    REQUIRE(result.items.size() == 5U);
    CHECK(result.items[0].type == hlaunch::core::ItemType::Folder);
    CHECK(result.items[0].name == "Folder");
    CHECK(result.items[1].type == hlaunch::core::ItemType::Application);
    CHECK(result.items[1].name == "Tool");
    CHECK(result.items[2].type == hlaunch::core::ItemType::Shortcut);
    CHECK(result.items[2].name == "Shortcut");
    CHECK(result.items[3].type == hlaunch::core::ItemType::File);
    CHECK(result.items[3].name == "notes.txt");
    CHECK(result.items[4].type == hlaunch::core::ItemType::Url);
    CHECK(result.items[4].name == "example.com");
}

TEST_CASE("PROD-DROP-001 launches an occupied item with dropped paths as arguments")
{
    const hlaunch::core::LaunchItem item{
        .id = "11111111-1111-4111-8111-111111111111",
        .type = hlaunch::core::ItemType::Application,
        .name = "ICO Maker",
        .target = R"(C:\Tools\ico-maker.exe)",
        .arguments = {"--quality", "mid"},
    };
    const auto launched = hlaunch::platform::windows::makeDropLaunchItem(
        item,
        {
            {hlaunch::platform::windows::DroppedSourceKind::Path,
             LR"(C:\Images\history.svg)"},
            {hlaunch::platform::windows::DroppedSourceKind::Url,
             L"https://example.com/ignored"},
            {hlaunch::platform::windows::DroppedSourceKind::Path,
             LR"(C:\资料\second image.svg)"},
        });

    REQUIRE(launched.has_value());
    CHECK(launched->id == item.id);
    CHECK(launched->target == item.target);
    CHECK((launched->arguments == std::vector<std::string>{
        "--quality",
        "mid",
        R"(C:\Images\history.svg)",
        "C:\\资料\\second image.svg",
    }));
    CHECK((item.arguments == std::vector<std::string>{"--quality", "mid"}));
}

TEST_CASE("PROD-DROP-001 does not turn a URL-only drop into launch arguments")
{
    const hlaunch::core::LaunchItem item{
        .type = hlaunch::core::ItemType::Application,
        .target = R"(C:\Tools\ico-maker.exe)",
    };

    CHECK_FALSE(hlaunch::platform::windows::makeDropLaunchItem(
        item,
        {{hlaunch::platform::windows::DroppedSourceKind::Url,
          L"https://example.com"}}));
}

TEST_CASE("PROD-DROP-001 rejects an invalid Unicode dropped path")
{
    const hlaunch::core::LaunchItem item{
        .type = hlaunch::core::ItemType::Application,
        .target = R"(C:\Tools\ico-maker.exe)",
    };
    const std::wstring invalidPath{static_cast<wchar_t>(0xD800)};

    CHECK_FALSE(hlaunch::platform::windows::makeDropLaunchItem(
        item,
        {{hlaunch::platform::windows::DroppedSourceKind::Path, invalidPath}}));
}

TEST_CASE("PROD-DROP-001 rejects script-like URL schemes")
{
    using hlaunch::platform::windows::DroppedSource;
    using hlaunch::platform::windows::DroppedSourceKind;
    const auto result = hlaunch::platform::windows::resolveDroppedSources({
        .sources = {
            DroppedSource{DroppedSourceKind::Url, L"javascript:alert(1)"},
            DroppedSource{DroppedSourceKind::Url, L"data:text/plain,hello"},
        },
    });

    CHECK(result.items.empty());
    CHECK(result.unsupportedCount == 2U);
}
