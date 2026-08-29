#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "core/data_model.h"
#include "core/item_operations.h"
#include "core/data_validation.h"
#include "infrastructure/filesystem/atomic_file.h"
#include "infrastructure/filesystem/config_save_worker.h"
#include "infrastructure/filesystem/data_paths.h"
#include "infrastructure/filesystem/data_store.h"
#include "infrastructure/filesystem/items_save_worker.h"

#include <Windows.h>
#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
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

TEST_CASE("DATA-CONFIG-001 defaults to a writable data root beside the executable")
{
    TemporaryDirectory temporary{};
    const auto executable = temporary.path() / L"app" / L"HLaunch.exe";
    const auto localAppData = temporary.path() / L"local";
    std::filesystem::create_directories(executable.parent_path());

    const auto paths = hlaunch::infrastructure::filesystem::resolveDataPaths({
        .executablePath = executable,
        .localAppDataOverride = localAppData,
    });
    REQUIRE(paths.has_value());
    CHECK(paths->portable);
    CHECK(paths->root == executable.parent_path() / L"data");
    CHECK(paths->configFile == paths->root / L"config.json");
    CHECK(paths->logDirectory == paths->root / L"logs");
    CHECK(paths->iconCacheDirectory == paths->root / L"cache" / L"icons");
    CHECK(std::filesystem::is_directory(paths->root));
}

TEST_CASE("DATA-CONFIG-001 falls back to LocalAppData when portable data is unavailable")
{
    TemporaryDirectory temporary{};
    const auto executable = temporary.path() / L"app" / L"HLaunch.exe";
    const auto localAppData = temporary.path() / L"local";
    std::filesystem::create_directories(executable.parent_path());
    std::ofstream blocker{executable.parent_path() / L"data"};
    REQUIRE(blocker.good());
    blocker.close();

    const auto paths = hlaunch::infrastructure::filesystem::resolveDataPaths({
        .executablePath = executable,
        .localAppDataOverride = localAppData,
    });
    REQUIRE(paths.has_value());
    CHECK_FALSE(paths->portable);
    CHECK(paths->root == localAppData / L"HLaunch");
    CHECK(std::filesystem::is_directory(paths->root));
}

TEST_CASE("DATA-CONFIG-001 falls back when an existing portable data file is locked")
{
    TemporaryDirectory temporary{};
    const auto executable = temporary.path() / L"app" / L"HLaunch.exe";
    const auto portableRoot = executable.parent_path() / L"data";
    const auto localAppData = temporary.path() / L"local";
    std::filesystem::create_directories(portableRoot);
    std::ofstream config{portableRoot / L"config.json"};
    REQUIRE(config.good());
    config << "{}";
    config.close();

    const auto lockedConfig = CreateFileW(
        (portableRoot / L"config.json").c_str(),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    REQUIRE(lockedConfig != INVALID_HANDLE_VALUE);

    const auto paths = hlaunch::infrastructure::filesystem::resolveDataPaths({
        .executablePath = executable,
        .localAppDataOverride = localAppData,
    });
    CHECK(CloseHandle(lockedConfig));
    REQUIRE(paths.has_value());
    CHECK_FALSE(paths->portable);
    CHECK(paths->root == localAppData / L"HLaunch");
}

TEST_CASE("DATA-CONFIG-001 reports failure when portable and fallback roots are unavailable")
{
    TemporaryDirectory temporary{};
    const auto executable = temporary.path() / L"app" / L"HLaunch.exe";
    const auto localAppData = temporary.path() / L"local";
    std::filesystem::create_directories(executable.parent_path());
    std::filesystem::create_directories(localAppData);
    std::ofstream portableBlocker{executable.parent_path() / L"data"};
    std::ofstream fallbackBlocker{localAppData / L"HLaunch"};
    REQUIRE(portableBlocker.good());
    REQUIRE(fallbackBlocker.good());
    portableBlocker.close();
    fallbackBlocker.close();

    const auto paths = hlaunch::infrastructure::filesystem::resolveDataPaths({
        .executablePath = executable,
        .localAppDataOverride = localAppData,
    });
    CHECK_FALSE(paths.has_value());
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
    CHECK(loaded->value->tabs.front().name == "默认");
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

TEST_CASE("DATA-CONFIG-001 queued config saves flush the latest snapshot on shutdown")
{
    TemporaryDirectory temporary{};
    const auto configPath = temporary.path() / L"config.json";
    std::atomic_uint32_t completionCount{};
    std::atomic_uint64_t latestRevision{};
    std::atomic_uint32_t failureCount{};

    hlaunch::core::ApplicationConfig first{};
    auto latest = first;
    latest.appearance.gridColumns = 7U;
    latest.activation.hotkey.enabled = false;

    {
        hlaunch::infrastructure::filesystem::ConfigSaveWorker worker{
            configPath,
            [&](const auto completion) {
                ++completionCount;
                latestRevision.store(completion.revision);
                if (completion.error) {
                    ++failureCount;
                }
            }};
        REQUIRE(worker.submit(1U, first));
        REQUIRE(worker.submit(2U, latest));
    }

    CHECK(completionCount.load() >= 1U);
    CHECK(latestRevision.load() == 2U);
    CHECK(failureCount.load() == 0U);
    const auto loaded = hlaunch::infrastructure::filesystem::loadConfig(configPath);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->value.has_value());
    CHECK(*loaded->value == latest);
}

TEST_CASE("DATA-CONFIG-001 background config save reports revision and failure")
{
    TemporaryDirectory temporary{};
    const auto configPath = temporary.path() / L"config.json";
    REQUIRE(std::filesystem::create_directory(
        hlaunch::infrastructure::filesystem::temporaryPathFor(configPath)));
    std::optional<hlaunch::infrastructure::filesystem::ConfigSaveCompletion> completion{};

    {
        hlaunch::infrastructure::filesystem::ConfigSaveWorker worker{
            configPath,
            [&](auto result) { completion = std::move(result); }};
        REQUIRE(worker.submit(42U, hlaunch::core::ApplicationConfig{}));
    }

    REQUIRE(completion.has_value());
    CHECK(completion->revision == 42U);
    CHECK(completion->error.has_value());
    CHECK_FALSE(std::filesystem::exists(configPath));
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
    hlaunch::core::normalizeGridSlots(latest);
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

TEST_CASE("PROD-ITEM-001 a throwing save notification cannot terminate the worker")
{
    TemporaryDirectory temporary{};
    const auto itemsPath = temporary.path() / L"items.json";
    REQUIRE(std::filesystem::create_directory(
        hlaunch::infrastructure::filesystem::temporaryPathFor(itemsPath)));
    std::atomic_uint32_t callbackCount{};

    hlaunch::core::ItemsDocument document{
        .tabs = {hlaunch::core::Tab{
            .id = "11111111-1111-4111-8111-111111111111",
            .name = "常用",
        }},
    };
    {
        hlaunch::infrastructure::filesystem::ItemsSaveWorker worker{
            itemsPath,
            [&callbackCount](const auto&) {
                ++callbackCount;
                throw std::runtime_error{"test notification failure"};
            }};
        worker.submit(std::move(document));
    }

    CHECK(callbackCount.load() == 1U);
    CHECK_FALSE(std::filesystem::exists(itemsPath));
}
