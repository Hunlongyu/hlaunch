#pragma once

#include "core/data_model.h"
#include "infrastructure/filesystem/data_store.h"

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace hlaunch::infrastructure::filesystem {

class ItemsSaveWorker final
{
  public:
    using FailureHandler = std::function<void(const StoreError &)>;

    ItemsSaveWorker(std::filesystem::path path, FailureHandler failureHandler);
    ~ItemsSaveWorker();

    ItemsSaveWorker(const ItemsSaveWorker &) = delete;
    ItemsSaveWorker &operator=(const ItemsSaveWorker &) = delete;

    void submit(core::ItemsDocument snapshot);

  private:
    void run() noexcept;

    std::filesystem::path path_{};
    FailureHandler failureHandler_{};
    std::mutex mutex_{};
    std::condition_variable condition_{};
    std::optional<core::ItemsDocument> pending_{};
    bool stopping_{};
    std::thread thread_{};
};

} // namespace hlaunch::infrastructure::filesystem
