#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/item_path_policy.h"
#include "platform/windows/shell_launcher.h"

#include <doctest/doctest.h>
#include <wil/resource.h>

#include <Windows.h>
#include <shobjidl.h>
#include <winrt/base.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::filesystem::path executableDirectory()
{
    std::wstring path(32'768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr,
        path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        throw std::runtime_error{"Cannot resolve the test executable path."};
    }
    path.resize(length);
    return std::filesystem::path{path}.parent_path();
}

std::string toUtf8(const std::wstring_view value)
{
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        throw std::runtime_error{"Cannot encode a test path as UTF-8."};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required,
        nullptr,
        nullptr) != required) {
        throw std::runtime_error{"Cannot encode a test path as UTF-8."};
    }
    return result;
}

std::vector<std::wstring> readArguments(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());

    std::uint32_t valueCount = 0;
    input.read(reinterpret_cast<char*>(&valueCount), sizeof(valueCount));
    std::vector<std::wstring> result{};
    result.reserve(valueCount);
    for (std::uint32_t index = 0; index < valueCount; ++index) {
        std::uint32_t characterCount = 0;
        input.read(reinterpret_cast<char*>(&characterCount), sizeof(characterCount));
        std::wstring value(characterCount, L'\0');
        input.read(
            reinterpret_cast<char*>(value.data()),
            static_cast<std::streamsize>(value.size() * sizeof(wchar_t)));
        result.push_back(std::move(value));
    }
    REQUIRE(input.good());
    return result;
}

void createShortcut(
    const std::filesystem::path& shortcutPath,
    const std::filesystem::path& targetPath,
    const std::wstring& parameters,
    const std::filesystem::path& workingDirectory)
{
    winrt::com_ptr<IShellLinkW> shellLink{};
    REQUIRE(SUCCEEDED(CoCreateInstance(
        CLSID_ShellLink,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(shellLink.put()))));
    REQUIRE(SUCCEEDED(shellLink->SetPath(targetPath.c_str())));
    REQUIRE(SUCCEEDED(shellLink->SetArguments(parameters.c_str())));
    REQUIRE(SUCCEEDED(shellLink->SetWorkingDirectory(workingDirectory.c_str())));
    REQUIRE(SUCCEEDED(shellLink->SetShowCmd(SW_SHOWMINNOACTIVE)));
    winrt::com_ptr<IPersistFile> persistFile{};
    REQUIRE(SUCCEEDED(shellLink->QueryInterface(IID_PPV_ARGS(persistFile.put()))));
    REQUIRE(SUCCEEDED(persistFile->Save(shortcutPath.c_str(), TRUE)));
}

} // namespace

TEST_CASE("PLAT-SHELL-001 quotes Windows arguments without changing their logical value")
{
    using hlaunch::platform::windows::quoteWindowsArgument;

    CHECK(quoteWindowsArgument(L"plain") == L"plain");
    CHECK(quoteWindowsArgument(L"") == L"\"\"");
    CHECK(quoteWindowsArgument(L"two words") == L"\"two words\"");
    CHECK(quoteWindowsArgument(L"quote\"inside") == L"\"quote\\\"inside\"");
    CHECK(quoteWindowsArgument(L"ends with slash\\") == L"\"ends with slash\\\\\"");
}

TEST_CASE("PLAT-SHELL-001 rejects malformed UTF-8 arguments")
{
    const std::vector<std::string> arguments{std::string{"\xC3\x28", 2}};
    const auto result = hlaunch::platform::windows::buildShellParameterString(arguments);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code
        == hlaunch::platform::windows::ShellLaunchErrorCode::InvalidUtf8);
}

TEST_CASE("PLAT-SHELL-001 builds a copyable command line with Windows quoting")
{
    const hlaunch::core::LaunchItem item{
        .target = "C:\\Program Files\\Example\\example.exe",
        .arguments = {"plain", "two words", ""},
    };
    const auto commandLine = hlaunch::platform::windows::buildShellCommandLine(item);

    REQUIRE(commandLine.has_value());
    CHECK(*commandLine
        == L"\"C:\\Program Files\\Example\\example.exe\" plain \"two words\" \"\"");
}

TEST_CASE("PLAT-SHELL-001 does not expose a file location action for URLs")
{
    const hlaunch::core::LaunchItem item{
        .type = hlaunch::core::ItemType::Url,
        .target = "https://example.com",
    };
    const auto result = hlaunch::platform::windows::openItemLocation(item);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().systemCode == ERROR_NOT_SUPPORTED);
}

TEST_CASE("DATA-CONFIG-001 resolves item paths against the executable directory")
{
    const hlaunch::platform::windows::ItemPathContext context{
        .executableDirectory = LR"(C:\Portable\HLaunch)",
    };
    const hlaunch::core::LaunchItem item{
        .type = hlaunch::core::ItemType::Application,
        .target = R"(tools\app.exe)",
        .workingDirectory = R"(.\work)",
        .icon = R"(assets\app.ico)",
    };

    const auto resolved = hlaunch::platform::windows::resolveItemPaths(item, context);

    REQUIRE(resolved.has_value());
    CHECK(resolved->target == R"(C:\Portable\HLaunch\tools\app.exe)");
    REQUIRE(resolved->workingDirectory.has_value());
    CHECK(*resolved->workingDirectory == R"(C:\Portable\HLaunch\work)");
    REQUIRE(resolved->icon.has_value());
    CHECK(*resolved->icon == R"(C:\Portable\HLaunch\assets\app.ico)");
}

TEST_CASE("DATA-CONFIG-001 expands environment variables without changing URL targets")
{
    constexpr auto variableName = L"HLAUNCH_ITEM_PATH_TEST_ROOT";
    std::wstring previousValue(32'768, L'\0');
    const DWORD previousLength = GetEnvironmentVariableW(
        variableName,
        previousValue.data(),
        static_cast<DWORD>(previousValue.size()));
    const bool hadPreviousValue = previousLength > 0 && previousLength < previousValue.size();
    if (hadPreviousValue) {
        previousValue.resize(previousLength);
    }
    REQUIRE(SetEnvironmentVariableW(variableName, LR"(C:\Environment Root)"));
    const auto restoreEnvironment = wil::scope_exit([&] {
        SetEnvironmentVariableW(
            variableName,
            hadPreviousValue ? previousValue.c_str() : nullptr);
    });

    const hlaunch::platform::windows::ItemPathContext context{
        .executableDirectory = LR"(C:\Portable\HLaunch)",
    };
    const hlaunch::core::LaunchItem item{
        .type = hlaunch::core::ItemType::Url,
        .target = "https://example.com/%HLAUNCH_ITEM_PATH_TEST_ROOT%",
        .workingDirectory = R"(%HLAUNCH_ITEM_PATH_TEST_ROOT%\work)",
        .icon = R"(%HLAUNCH_ITEM_PATH_TEST_ROOT%\app.ico)",
    };

    const auto resolved = hlaunch::platform::windows::resolveItemPaths(item, context);

    REQUIRE(resolved.has_value());
    CHECK(resolved->target == item.target);
    REQUIRE(resolved->workingDirectory.has_value());
    CHECK(*resolved->workingDirectory == R"(C:\Environment Root\work)");
    REQUIRE(resolved->icon.has_value());
    CHECK(*resolved->icon == R"(C:\Environment Root\app.ico)");
}

TEST_CASE("DATA-CONFIG-001 stores portable paths inside the executable tree as relative")
{
    const hlaunch::platform::windows::ItemPathContext context{
        .executableDirectory = LR"(C:\Portable\HLaunch)",
        .portable = true,
    };
    const hlaunch::core::LaunchItem item{
        .type = hlaunch::core::ItemType::Application,
        .target = R"(c:\portable\hlaunch\Tools\App.exe)",
        .workingDirectory = R"(C:\Portable\HLaunch\Work)",
        .icon = R"(D:\Shared\App.ico)",
    };

    const auto portable = hlaunch::platform::windows::makeItemPathsPortable(item, context);

    REQUIRE(portable.has_value());
    CHECK(portable->target == R"(Tools\App.exe)");
    REQUIRE(portable->workingDirectory.has_value());
    CHECK(*portable->workingDirectory == "Work");
    REQUIRE(portable->icon.has_value());
    CHECK(*portable->icon == R"(D:\Shared\App.ico)");

    const auto resolved = hlaunch::platform::windows::resolveItemPaths(*portable, context);
    REQUIRE(resolved.has_value());
    CHECK(resolved->target == R"(C:\Portable\HLaunch\Tools\App.exe)");
    CHECK(*resolved->workingDirectory == R"(C:\Portable\HLaunch\Work)");
    CHECK(*resolved->icon == R"(D:\Shared\App.ico)");
}

TEST_CASE("DATA-CONFIG-001 rejects Windows paths that depend on the current directory")
{
    const hlaunch::platform::windows::ItemPathContext context{
        .executableDirectory = LR"(C:\Portable\HLaunch)",
    };
    for (const std::string target : {R"(C:tools\app.exe)", R"(\tools\app.exe)"}) {
        const hlaunch::core::LaunchItem item{
            .type = hlaunch::core::ItemType::Application,
            .target = target,
        };
        const auto resolved = hlaunch::platform::windows::resolveItemPaths(item, context);
        REQUIRE_FALSE(resolved.has_value());
        CHECK(resolved.error().code
            == hlaunch::platform::windows::ItemPathErrorCode::InvalidPath);
    }
}

TEST_CASE("PLAT-SHELL-001 ShellExecuteEx preserves separate logical arguments")
{
    const auto directory = executableDirectory();
    const auto probePath = directory / L"hlaunch_shell_probe.exe";
    REQUIRE(std::filesystem::is_regular_file(probePath));

    const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto outputPath = std::filesystem::temp_directory_path()
        / (L"hlaunch-shell-" + std::to_wstring(uniqueValue) + L".bin");
    const auto cleanup = wil::scope_exit([&] {
        std::error_code error{};
        std::filesystem::remove(outputPath, error);
    });

    const hlaunch::core::LaunchItem item{
        .id = "11111111-1111-4111-8111-111111111111",
        .type = hlaunch::core::ItemType::Application,
        .name = "Shell probe",
        .target = toUtf8(probePath.wstring()),
        .arguments = {
            toUtf8(outputPath.wstring()),
            "",
            "two words",
            "quote\"inside",
            "tail\\",
        },
    };

    auto result = hlaunch::platform::windows::launchItem(nullptr, item);
    REQUIRE(result.has_value());
    REQUIRE(result->process);
    REQUIRE(WaitForSingleObject(result->process.get(), 5'000) == WAIT_OBJECT_0);

    DWORD exitCode = 0;
    REQUIRE(GetExitCodeProcess(result->process.get(), &exitCode));
    REQUIRE(exitCode == 0);
    CHECK(readArguments(outputPath) == std::vector<std::wstring>{
        L"",
        L"two words",
        L"quote\"inside",
        L"tail\\",
    });
}

TEST_CASE("PLAT-SHELL-001 resolves shortcut target arguments and working directory")
{
    const auto oleResult = OleInitialize(nullptr);
    REQUIRE(SUCCEEDED(oleResult));
    const auto oleCleanup = wil::scope_exit([&] { OleUninitialize(); });

    const auto directory = executableDirectory();
    const auto probePath = directory / L"hlaunch_shell_probe.exe";
    REQUIRE(std::filesystem::is_regular_file(probePath));

    const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto outputPath = std::filesystem::temp_directory_path()
        / (L"hlaunch-shortcut-" + std::to_wstring(uniqueValue) + L".bin");
    const auto shortcutPath = std::filesystem::temp_directory_path()
        / (L"hlaunch-shortcut-" + std::to_wstring(uniqueValue) + L".lnk");
    const auto cleanup = wil::scope_exit([&] {
        std::error_code error{};
        std::filesystem::remove(outputPath, error);
        std::filesystem::remove(shortcutPath, error);
    });

    createShortcut(
        shortcutPath,
        probePath,
        hlaunch::platform::windows::quoteWindowsArgument(outputPath.wstring())
            + L" \"shortcut argument\"",
        directory);
    const hlaunch::core::LaunchItem item{
        .id = "22222222-2222-4222-8222-222222222222",
        .type = hlaunch::core::ItemType::Shortcut,
        .name = "Shortcut probe",
        .target = toUtf8(shortcutPath.wstring()),
        .arguments = {"item argument"},
    };

    auto result = hlaunch::platform::windows::launchItem(nullptr, item);
    REQUIRE(result.has_value());
    REQUIRE(result->process);
    REQUIRE(WaitForSingleObject(result->process.get(), 5'000) == WAIT_OBJECT_0);
    DWORD exitCode{};
    REQUIRE(GetExitCodeProcess(result->process.get(), &exitCode));
    REQUIRE(exitCode == 0U);
    CHECK(readArguments(outputPath) == std::vector<std::wstring>{
        L"shortcut argument",
        L"item argument",
    });
}
