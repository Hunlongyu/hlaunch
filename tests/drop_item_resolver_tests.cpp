#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "platform/windows/drop_item_resolver.h"
#include "platform/windows/shell_launcher.h"
#include "platform/windows/item_path_policy.h"

#include <Windows.h>
#include <ShlObj.h>
#include <propsys.h>
#include <propkey.h>
#include <winrt/base.h>
#include <wil/resource.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
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

std::string toUtf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path fromUtf8(const std::string& value)
{
    return std::filesystem::path{std::u8string{value.begin(), value.end()}};
}

std::filesystem::path probePath()
{
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    REQUIRE(length > 0);
    REQUIRE(length < path.size());
    path.resize(length);
    const auto probe = std::filesystem::path{path}.parent_path() / L"hlaunch_shell_probe.exe";
    REQUIRE(std::filesystem::exists(probe));
    return probe;
}

void createShortcut(const std::filesystem::path& path, const std::filesystem::path& target,
    const std::wstring& arguments = {}, const std::filesystem::path& directory = {},
    const int showCommand = SW_SHOWNORMAL, const int iconIndex = 0, const bool runAs = false,
    const bool appIdentity = false)
{
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(apartment));
    const auto cleanup = wil::scope_exit([&] { CoUninitialize(); });
    winrt::com_ptr<IShellLinkW> link;
    winrt::com_ptr<IPersistFile> file;
    REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(link.put()))));
    REQUIRE(SUCCEEDED(link->SetPath(target.c_str())));
    REQUIRE(SUCCEEDED(link->SetArguments(arguments.c_str())));
    REQUIRE(SUCCEEDED(link->SetWorkingDirectory(directory.c_str())));
    REQUIRE(SUCCEEDED(link->SetShowCmd(showCommand)));
    REQUIRE(SUCCEEDED(link->SetIconLocation(target.c_str(), iconIndex)));
    if (appIdentity) {
        winrt::com_ptr<IPropertyStore> properties;
        REQUIRE(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(properties.put()))));
        PROPVARIANT value{};
        value.vt = VT_LPWSTR;
        value.pwszVal = const_cast<wchar_t*>(L"HLaunch.Shortcut.Import.Test");
        REQUIRE(SUCCEEDED(properties->SetValue(PKEY_AppUserModel_ID, value)));
    }
    if (runAs) {
        winrt::com_ptr<IShellLinkDataList> data;
        REQUIRE(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(data.put()))));
        DWORD flags{};
        REQUIRE(SUCCEEDED(data->GetFlags(&flags)));
        REQUIRE(SUCCEEDED(data->SetFlags(flags | SLDF_RUNAS_USER)));
    }
    REQUIRE(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(file.put()))));
    REQUIRE(SUCCEEDED(file->Save(path.c_str(), TRUE)));
}

hlaunch::platform::windows::DropImportResult importLink(
    const std::filesystem::path& path, const std::filesystem::path& store)
{
    return hlaunch::platform::windows::resolveDroppedSources({
        .sources = {{hlaunch::platform::windows::DroppedSourceKind::Path, path.wstring()}},
    }, store);
}

std::vector<std::wstring> readArguments(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    std::uint32_t count{};
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    REQUIRE(count < 20);
    std::vector<std::wstring> result;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t length{};
        stream.read(reinterpret_cast<char*>(&length), sizeof(length));
        REQUIRE(length < 32768);
        std::wstring text(length, L'\0');
        stream.read(reinterpret_cast<char*>(text.data()), length * sizeof(wchar_t));
        result.push_back(std::move(text));
    }
    REQUIRE(stream.good());
    return result;
}

void launchAndWait(const hlaunch::core::LaunchItem& item)
{
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(apartment));
    const auto cleanup = wil::scope_exit([&] { CoUninitialize(); });
    auto result = hlaunch::platform::windows::launchItem(nullptr, item);
    REQUIRE(result.has_value());
    REQUIRE(result->process);
    REQUIRE(WaitForSingleObject(result->process.get(), 5000) == WAIT_OBJECT_0);
    DWORD exitCode{};
    REQUIRE(GetExitCodeProcess(result->process.get(), &exitCode));
    CHECK(exitCode == 0);
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
    REQUIRE(std::filesystem::copy_file(probePath(), executable));
    createShortcut(shortcut, executable);
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
    CHECK(result.items[2].type == hlaunch::core::ItemType::Application);
    CHECK(result.items[2].name == "Shortcut");
    // Shell may expand an 8.3 path from TEMP while resolving the shortcut.
    CHECK(std::filesystem::equivalent(fromUtf8(result.items[2].target), executable));
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

TEST_CASE("PROD-DROP-001 imported shortcut survives source deletion and preserves arguments and cwd")
{
    TemporaryDirectory temporary;
    const auto link = temporary.path() / L"应用 link.lnk";
    const auto store = temporary.path() / L"shortcuts";
    const auto executable = probePath();
    const std::vector<std::string> arguments{
        "output.bin", "", "two words", "quote\"inside", "tail with space\\", "中文"};
    const auto command = hlaunch::platform::windows::buildShellParameterString(arguments);
    REQUIRE(command);
    createShortcut(link, executable, *command, temporary.path());
    const auto imported = importLink(link, store);
    REQUIRE(imported.items.size() == 1);
    const auto& item = imported.items.front();
    CHECK(item.type == hlaunch::core::ItemType::Application);
    CHECK(item.name == "应用 link");
    CHECK(item.target == toUtf8(executable));
    CHECK(item.arguments == arguments);
    CHECK(item.workingDirectory == toUtf8(temporary.path()));
    CHECK(item.icon == toUtf8(executable));
    REQUIRE(std::filesystem::remove(link));
    launchAndWait(item);
    CHECK(readArguments(temporary.path() / L"output.bin") == std::vector<std::wstring>{
        L"", L"two words", L"quote\"inside", L"tail with space\\", L"中文"});
    CHECK_FALSE(std::filesystem::exists(store));
}

TEST_CASE("PROD-DROP-001 preserves elevation without elevating during import")
{
    TemporaryDirectory temporary;
    const auto link = temporary.path() / L"admin.lnk";
    createShortcut(link, probePath(), {}, {}, SW_SHOWNORMAL, 0, true);
    const auto imported = importLink(link, temporary.path() / L"shortcuts");
    REQUIRE(imported.items.size() == 1);
    CHECK(imported.items[0].type == hlaunch::core::ItemType::Application);
    CHECK(imported.items[0].runAsAdministrator);
}

TEST_CASE("PROD-DROP-001 retains special links and reuses snapshots across repeated imports")
{
    TemporaryDirectory temporary;
    const auto link = temporary.path() / L"special.lnk";
    const auto store = temporary.path() / L"shortcuts";
    createShortcut(link, probePath(), L"output.bin \"special argument\"", temporary.path(),
        SW_SHOWMINNOACTIVE, -101);
    const auto imported = importLink(link, store);
    REQUIRE(imported.items.size() == 1);
    const auto& item = imported.items.front();
    REQUIRE(item.type == hlaunch::core::ItemType::Shortcut);
    REQUIRE(item.target != toUtf8(link));
    CHECK(fromUtf8(item.target).parent_path() == store);
    const auto repeated = importLink(link, store);
    REQUIRE(repeated.items.size() == 1);
    CHECK(repeated.items[0].target == item.target);
    const auto portable = hlaunch::platform::windows::makeItemPathsPortable(item,
        {.executableDirectory = temporary.path(), .portable = true});
    REQUIRE(portable);
    CHECK(fromUtf8(portable->target).is_relative());
    REQUIRE(std::filesystem::remove(link));
    launchAndWait(item);
    CHECK(readArguments(temporary.path() / L"output.bin")
        == std::vector<std::wstring>{L"special argument"});
}

TEST_CASE("PROD-DROP-001 rejects corrupt links and does not fall back to the source on copy failure")
{
    TemporaryDirectory temporary;
    const auto link = temporary.path() / L"broken.lnk";
    const auto store = temporary.path() / L"not-a-directory";
    createFile(link);
    CHECK(importLink(link, store).unsupportedCount == 1);
    createShortcut(link, probePath(), {}, {}, SW_SHOWMAXIMIZED);
    createFile(store);
    const auto imported = importLink(link, store);
    CHECK(imported.items.empty());
    CHECK(imported.unsupportedCount == 1);
    CHECK(std::filesystem::exists(link));
}

TEST_CASE("PROD-DROP-001 initializes COM on the background import thread")
{
    TemporaryDirectory temporary;
    const auto link = temporary.path() / L"background.lnk";
    createShortcut(link, probePath());
    std::promise<hlaunch::platform::windows::DropImportResult> completed;
    auto result = completed.get_future();
    hlaunch::platform::windows::DropItemResolver resolver{
        [&](auto value) { completed.set_value(std::move(value)); }, temporary.path() / L"shortcuts"};
    resolver.submit({
        .sources = {{hlaunch::platform::windows::DroppedSourceKind::Path, link.wstring()}},
    });
    REQUIRE(result.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
    const auto imported = result.get();
    REQUIRE(imported.items.size() == 1);
    CHECK(imported.items[0].type == hlaunch::core::ItemType::Application);
}

TEST_CASE("PROD-DROP-001 preserves package identity metadata in a private shortcut")
{
    TemporaryDirectory temporary;
    const auto link = temporary.path() / L"identity.lnk";
    createShortcut(link, probePath(), {}, {}, SW_SHOWNORMAL, 0, false, true);
    const auto imported = importLink(link, temporary.path() / L"shortcuts");
    REQUIRE(imported.items.size() == 1);
    REQUIRE(imported.items[0].type == hlaunch::core::ItemType::Shortcut);
    REQUIRE(std::filesystem::remove(link));
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(apartment));
    const auto cleanup = wil::scope_exit([&] { CoUninitialize(); });
    winrt::com_ptr<IShellLinkW> retained;
    REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(retained.put()))));
    auto file = retained.as<IPersistFile>();
    REQUIRE(SUCCEEDED(file->Load(fromUtf8(imported.items[0].target).c_str(), STGM_READ)));
    auto properties = retained.as<IPropertyStore>();
    PROPVARIANT value{};
    REQUIRE(SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID, &value)));
    const auto clear = wil::scope_exit([&] { PropVariantClear(&value); });
    REQUIRE(value.vt == VT_LPWSTR);
    CHECK(std::wstring{value.pwszVal} == L"HLaunch.Shortcut.Import.Test");
}
