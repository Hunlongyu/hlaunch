#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "infrastructure/logging/diagnostic_log.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        path_ = std::filesystem::temp_directory_path()
            / (L"hlaunch-log-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-"
               + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        hlaunch::infrastructure::logging::shutdown();
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_{};
};

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::vector<std::filesystem::path> logFiles(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files{};
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == L".log") {
            files.push_back(entry.path());
        }
    }
    return files;
}

} // namespace

TEST_CASE("QUALITY-LOG-001 writes structured sanitized diagnostic events")
{
    TemporaryDirectory temporary{};
    const auto initialized = hlaunch::infrastructure::logging::initialize({
        .directory = temporary.path(),
    });
    REQUIRE(initialized.has_value());

    hlaunch::infrastructure::logging::write(
        hlaunch::infrastructure::logging::Level::Info,
        "window_created\nforged_event");
    hlaunch::infrastructure::logging::writeSystemError(
        hlaunch::infrastructure::logging::Level::Error,
        "hotkey_registration_failed",
        ERROR_HOTKEY_ALREADY_REGISTERED);
    hlaunch::infrastructure::logging::shutdown();

    const auto content = readFile(*initialized);
    CHECK(content.find("[INFO]") != std::string::npos);
    CHECK(content.find("pid=") != std::string::npos);
    CHECK(content.find("tid=") != std::string::npos);
    CHECK(content.find("event=window_created forged_event\r\n") != std::string::npos);
    CHECK(content.find("system_code=1409") != std::string::npos);
}

TEST_CASE("QUALITY-LOG-001 caps each diagnostic log file")
{
    TemporaryDirectory temporary{};
    constexpr std::uintmax_t limit = 256;
    const auto initialized = hlaunch::infrastructure::logging::initialize({
        .directory = temporary.path(),
        .maxFileBytes = limit,
    });
    REQUIRE(initialized.has_value());

    for (int index = 0; index < 20; ++index) {
        hlaunch::infrastructure::logging::write(
            hlaunch::infrastructure::logging::Level::Debug,
            "repeated_diagnostic_event");
    }
    hlaunch::infrastructure::logging::shutdown();

    CHECK(std::filesystem::file_size(*initialized) <= limit);
}

TEST_CASE("QUALITY-LOG-001 removes expired and excess log files")
{
    TemporaryDirectory temporary{};
    for (int index = 0; index < 5; ++index) {
        const auto path = temporary.path() / (L"HLaunch-old-" + std::to_wstring(index) + L".log");
        std::ofstream{path} << index;
        std::filesystem::last_write_time(
            path,
            std::filesystem::file_time_type::clock::now() - std::chrono::hours{index + 1});
    }
    const auto expired = temporary.path() / L"HLaunch-expired.log";
    std::ofstream{expired} << "expired";
    std::filesystem::last_write_time(
        expired,
        std::filesystem::file_time_type::clock::now() - std::chrono::hours{24 * 8});

    const auto initialized = hlaunch::infrastructure::logging::initialize({
        .directory = temporary.path(),
        .maxFiles = 3,
        .maxAge = std::chrono::hours{24 * 7},
    });
    REQUIRE(initialized.has_value());
    hlaunch::infrastructure::logging::shutdown();

    CHECK(logFiles(temporary.path()).size() == 3);
    CHECK_FALSE(std::filesystem::exists(expired));
}

TEST_CASE("QUALITY-LOG-001 records exception code and instruction address")
{
    TemporaryDirectory temporary{};
    const auto initialized = hlaunch::infrastructure::logging::initialize({
        .directory = temporary.path(),
    });
    REQUIRE(initialized.has_value());

    hlaunch::infrastructure::logging::writeUnhandledException(0xC0000005UL, 0x1234U);
    hlaunch::infrastructure::logging::shutdown();

    const auto content = readFile(*initialized);
    CHECK(content.find("event=unhandled_exception") != std::string::npos);
    CHECK(content.find("exception_code=0xC0000005") != std::string::npos);
    CHECK(content.find("instruction_address=0x1234") != std::string::npos);
}
