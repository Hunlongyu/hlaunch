#pragma once

#include "core/data_model.h"
#include "infrastructure/filesystem/data_store.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace hlaunch::infrastructure::filesystem {

struct ConfigSaveCompletion {
    std::uint64_t revision{};
    core::ApplicationConfig snapshot{};
    std::optional<StoreError> error{};
};

class ConfigSaveWorker final {
public:
    using CompletionHandler = std::function<void(ConfigSaveCompletion)>;

    ConfigSaveWorker(std::filesystem::path path, CompletionHandler completionHandler);
    ~ConfigSaveWorker();

    ConfigSaveWorker(const ConfigSaveWorker&) = delete;
    ConfigSaveWorker& operator=(const ConfigSaveWorker&) = delete;

    [[nodiscard]] bool submit(
        std::uint64_t revision,
        core::ApplicationConfig snapshot);

private:
    struct PendingSave {
        std::uint64_t revision{};
        core::ApplicationConfig snapshot{};
    };

    void run() noexcept;

    std::filesystem::path path_{};
    CompletionHandler completionHandler_{};
    std::mutex mutex_{};
    std::condition_variable condition_{};
    std::optional<PendingSave> pending_{};
    bool stopping_{};
    std::thread thread_{};
};

} // namespace hlaunch::infrastructure::filesystem
