#include "infrastructure/filesystem/items_save_worker.h"

#include <utility>

namespace hlaunch::infrastructure::filesystem {

ItemsSaveWorker::ItemsSaveWorker(std::filesystem::path path, FailureHandler failureHandler)
    : path_(std::move(path)), failureHandler_(std::move(failureHandler)), thread_([this] { run(); })
{}

ItemsSaveWorker::~ItemsSaveWorker()
{
    {
        const std::scoped_lock lock{mutex_};
        stopping_ = true;
    }
    condition_.notify_one();
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void ItemsSaveWorker::submit(core::ItemsDocument snapshot)
{
    {
        const std::scoped_lock lock{mutex_};
        if (stopping_)
            return;
        pending_ = std::move(snapshot);
    }
    condition_.notify_one();
}

void ItemsSaveWorker::run()
{
    for (;;)
    {
        std::optional<core::ItemsDocument> snapshot{};
        {
            std::unique_lock lock{mutex_};
            condition_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (!pending_ && stopping_)
                return;
            snapshot = std::move(pending_);
            pending_.reset();
        }
        if (const auto result = saveItems(path_, *snapshot); !result && failureHandler_)
        {
            failureHandler_(result.error());
        }
    }
}

} // namespace hlaunch::infrastructure::filesystem
