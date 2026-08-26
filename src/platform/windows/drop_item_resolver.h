#pragma once

#include "core/data_model.h"
#include "platform/windows/drop_target.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace hlaunch::platform::windows {

struct DropImportRequest
{
    std::size_t targetTabIndex{};
    std::vector<DroppedSource> sources{};
};

struct DropImportResult
{
    std::size_t targetTabIndex{};
    std::vector<core::LaunchItem> items{};
    std::size_t unsupportedCount{};
    bool failed{};
};

[[nodiscard]] DropImportResult resolveDroppedSources(const DropImportRequest &request);

class DropItemResolver final
{
  public:
    using CompletionHandler = std::function<void(DropImportResult)>;

    explicit DropItemResolver(CompletionHandler completionHandler);
    ~DropItemResolver();

    DropItemResolver(const DropItemResolver &) = delete;
    DropItemResolver &operator=(const DropItemResolver &) = delete;

    void submit(DropImportRequest request);

  private:
    void run();

    CompletionHandler completionHandler_{};
    std::mutex mutex_{};
    std::condition_variable condition_{};
    std::deque<DropImportRequest> pending_{};
    bool stopping_{};
    std::thread thread_{};
};

} // namespace hlaunch::platform::windows
