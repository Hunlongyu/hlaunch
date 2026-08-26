#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/shell_launcher.h"

#include <doctest/doctest.h>
#include <wil/resource.h>

#include <Windows.h>

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
