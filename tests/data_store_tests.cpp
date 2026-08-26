#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "core/data_model.h"
#include "core/data_validation.h"
#include "infrastructure/filesystem/atomic_file.h"
#include "infrastructure/filesystem/data_paths.h"
#include "infrastructure/filesystem/data_store.h"
#include "infrastructure/filesystem/items_save_worker.h"

#include <Windows.h>
#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        const auto sequence = nextSequence_.fetch_add(1);
        path_ = std::filesystem::temp_directory_path()
            / (L"HLaunchTests-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                + std::to_wstring(sequence));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error{};
        const auto expectedParent = std::filesystem::temp_directory_path().lexically_normal();
        const auto resolvedParent = path_.parent_path().lexically_normal();
        if (resolvedParent == expectedParent
            && path_.filename().wstring().starts_with(L"HLaunchTests-")) {
            std::filesystem::remove_all(path_, error);
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    inline static std::atomic_uint32_t nextSequence_{};
    std::filesystem::path path_{};
};

} // namespace

TEST_CASE("DATA-CONFIG-001 resolves standard and forced portable data roots")
{
    TemporaryDirectory temporary{};
    const auto executable = temporary.path() / L"app" / L"HLaunch.exe";
    const auto localAppData = temporary.path() / L"local";
    std::filesystem::create_directories(executable.parent_path());

    const auto standard = hlaunch::infrastructure::filesystem::resolveDataPaths({
        .executablePath = executable,
        .localAppDataOverride = localAppData,
    });
    REQUIRE(standard.has_value());
    CHECK_FALSE(standard->portable);
    CHECK(standard->root == localAppData / L"HLaunch");
    CHECK(standard->configFile == standard->root / L"config.json");
    CHECK(standard->logDirectory == standard->root / L"logs");

    const auto portable = hlaunch::infrastructure::filesystem::resolveDataPaths({
        .executablePath = executable,
        .localAppDataOverride = localAppData,
        .forcePortable = true,
    });
    REQUIRE(portable.has_value());
    CHECK(portable->portable);
    CHECK(portable->root == executable.parent_path() / L"data");
    CHECK(portable->logDirectory == portable->root / L"logs");
}

TEST_CASE("DATA-CONFIG-001 portable.flag selects the portable data root")
{
    TemporaryDirectory temporary{};
    const auto executable = temporary.path() / L"app" / L"HLaunch.exe";
    std::filesystem::create_directories(executable.parent_path());
    std::ofstream flag{executable.parent_path() / L"portable.flag"};
    REQUIRE(flag.good());
    flag.close();

    const auto paths = hlaunch::infrastructure::filesystem::resolveDataPaths({
        .executablePath = executable,
        .localAppDataOverride = temporary.path() / L"local",
    });
    REQUIRE(paths.has_value());
    CHECK(paths->portable);
    CHECK(paths->root == executable.parent_path() / L"data");
}

TEST_CASE("PROD-ITEM-001 a missing items file provides an editable default tab")
{
    TemporaryDirectory temporary{};
    const auto loaded =
        hlaunch::infrastructure::filesystem::loadItems(temporary.path() / L"items.json");

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->value.has_value());
    CHECK(loaded->source == hlaunch::infrastructure::filesystem::LoadSource::Defaults);
    REQUIRE(loaded->value->tabs.size() == 1U);
    CHECK(loaded->value->tabs.front().name == "常用");
    CHECK(loaded->value->tabs.front().items.empty());
    CHECK(hlaunch::core::validateItemsDocument(*loaded->value).empty());
}

TEST_CASE("DATA-CONFIG-001 atomic saves retain one previous valid backup")
{
    TemporaryDirectory temporary{};
    const auto configPath = temporary.path() / L"config.json";

    hlaunch::core::ApplicationConfig first{};
    REQUIRE(hlaunch::infrastructure::filesystem::saveConfig(configPath, first).has_value());

    auto second = first;
    second.activation.hotkey.enabled = false;
    REQUIRE(hlaunch::infrastructure::filesystem::saveConfig(configPath, second).has_value());

    const auto primary = hlaunch::infrastructure::filesystem::loadConfig(configPath);
    REQUIRE(primary.has_value());
    REQUIRE(primary->value.has_value());
    CHECK_FALSE(primary->value->activation.hotkey.enabled);

    const auto backup = hlaunch::infrastructure::filesystem::loadConfig(
        hlaunch::infrastructure::filesystem::backupPathFor(configPath));
    REQUIRE(backup.has_value());
    REQUIRE(backup->value.has_value());
    CHECK(backup->value->activation.hotkey.enabled);
}

TEST_CASE("DATA-CONFIG-001 a damaged primary can be recovered from its backup")
{
    TemporaryDirectory temporary{};
    const auto configPath = temporary.path() / L"config.json";

    hlaunch::core::ApplicationConfig first{};
    REQUIRE(hlaunch::infrastructure::filesystem::saveConfig(configPath, first).has_value());
    auto second = first;
    second.activation.hotkey.enabled = false;
    REQUIRE(hlaunch::infrastructure::filesystem::saveConfig(configPath, second).has_value());
    REQUIRE(hlaunch::infrastructure::filesystem::writeFileAtomically(
        configPath,
        "{broken-json").has_value());

    const auto recovered = hlaunch::infrastructure::filesystem::loadConfig(configPath);
    REQUIRE(recovered.has_value());
    REQUIRE(recovered->value.has_value());
    CHECK(recovered->recoveredFromBackup());
    CHECK_FALSE(recovered->value->activation.hotkey.enabled);
    CHECK_FALSE(recovered->issues.empty());
}

TEST_CASE("DATA-CONFIG-001 a temporary-file failure preserves the primary")
{
    TemporaryDirectory temporary{};
    const auto configPath = temporary.path() / L"config.json";
    hlaunch::core::ApplicationConfig original{};
    REQUIRE(hlaunch::infrastructure::filesystem::saveConfig(configPath, original).has_value());

    const auto blockingTemporaryPath =
        hlaunch::infrastructure::filesystem::temporaryPathFor(configPath);
    REQUIRE(std::filesystem::create_directory(blockingTemporaryPath));

    auto changed = original;
    changed.activation.hotkey.enabled = false;
    const auto failedSave = hlaunch::infrastructure::filesystem::saveConfig(
        configPath,
        changed);
    CHECK_FALSE(failedSave.has_value());

    const auto loaded = hlaunch::infrastructure::filesystem::loadConfig(configPath);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->value.has_value());
    CHECK(loaded->value->activation.hotkey.enabled);
}

TEST_CASE("PROD-ITEM-001 queued item saves flush the latest snapshot on shutdown")
{
    TemporaryDirectory temporary{};
    const auto itemsPath = temporary.path() / L"items.json";
    std::atomic_uint32_t failureCount{};

    hlaunch::core::ItemsDocument first{};
    first.tabs.push_back({
        .id = "11111111-1111-4111-8111-111111111111",
        .name = "常用",
    });
    first.tabs.front().items.push_back({
        .id = "22222222-2222-4222-8222-222222222222",
        .name = "第一项",
        .target = "first.exe",
    });

    auto latest = first;
    latest.tabs.front().items.front().name = "最终名称";
    latest.tabs.front().items.front().target = "latest.exe";

    {
        hlaunch::infrastructure::filesystem::ItemsSaveWorker worker{
            itemsPath, [&failureCount](const auto&) { ++failureCount; }};
        worker.submit(first);
        worker.submit(latest);
    }

    CHECK(failureCount.load() == 0U);
    const auto loaded = hlaunch::infrastructure::filesystem::loadItems(itemsPath);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->value.has_value());
    CHECK(*loaded->value == latest);
}

TEST_CASE("PROD-ITEM-001 background save failures reach the notification callback")
{
    TemporaryDirectory temporary{};
    const auto itemsPath = temporary.path() / L"items.json";
    REQUIRE(std::filesystem::create_directory(
        hlaunch::infrastructure::filesystem::temporaryPathFor(itemsPath)));
    std::atomic_uint32_t failureCount{};

    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "常用",
        }},
    };
    {
        hlaunch::infrastructure::filesystem::ItemsSaveWorker worker{
            itemsPath, [&failureCount](const auto&) { ++failureCount; }};
        worker.submit(std::move(document));
    }

    CHECK(failureCount.load() == 1U);
    CHECK_FALSE(std::filesystem::exists(itemsPath));
}
