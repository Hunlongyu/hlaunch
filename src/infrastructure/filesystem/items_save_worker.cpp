#include "infrastructure/filesystem/items_save_worker.h"

#include <Windows.h>

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

void ItemsSaveWorker::run() noexcept
{
    try {
        for (;;) {
            std::optional<core::ItemsDocument> snapshot{};
            {
                std::unique_lock lock{mutex_};
                condition_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                if (!pending_ && stopping_)
                    return;
                snapshot = std::move(pending_);
                pending_.reset();
            }

            std::optional<StoreError> failure{};
            try {
                if (const auto result = saveItems(path_, *snapshot); !result) {
                    failure = result.error();
                }
            }
            catch (...) {
                try {
                    failure = StoreError{
                        .code = StoreErrorCode::Unexpected,
                        .path = path_,
                        .systemCode = ERROR_UNHANDLED_EXCEPTION,
                        .message = "unexpected exception while saving items",
                    };
                }
                catch (...) {
                }
            }
            if (failure && failureHandler_) {
                try {
                    failureHandler_(*failure);
                }
                catch (...) {
                    // A background failure must not terminate the application.
                }
            }
        }
    }
    catch (...) {
        try {
            const std::scoped_lock lock{mutex_};
            stopping_ = true;
            pending_.reset();
        }
        catch (...) {
        }
    }
}

} // namespace hlaunch::infrastructure::filesystem
