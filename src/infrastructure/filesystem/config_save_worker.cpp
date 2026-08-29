#include "infrastructure/filesystem/config_save_worker.h"

#include <Windows.h>

#include <utility>

namespace hlaunch::infrastructure::filesystem {

ConfigSaveWorker::ConfigSaveWorker(
    std::filesystem::path path,
    CompletionHandler completionHandler)
    : path_(std::move(path)),
      completionHandler_(std::move(completionHandler)),
      thread_([this] { run(); })
{}

ConfigSaveWorker::~ConfigSaveWorker()
{
    {
        const std::scoped_lock lock{mutex_};
        stopping_ = true;
    }
    condition_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool ConfigSaveWorker::submit(
    const std::uint64_t revision,
    core::ApplicationConfig snapshot)
{
    {
        const std::scoped_lock lock{mutex_};
        if (stopping_) {
            return false;
        }
        pending_ = PendingSave{revision, std::move(snapshot)};
    }
    condition_.notify_one();
    return true;
}

void ConfigSaveWorker::run() noexcept
{
    try {
        for (;;) {
            std::optional<PendingSave> pending{};
            {
                std::unique_lock lock{mutex_};
                condition_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                if (!pending_ && stopping_) {
                    return;
                }
                pending = std::move(pending_);
                pending_.reset();
            }

            try {
                ConfigSaveCompletion completion{
                    .revision = pending->revision,
                    .snapshot = std::move(pending->snapshot),
                };
                try {
                    if (const auto result = saveConfig(path_, completion.snapshot); !result) {
                        completion.error = result.error();
                    }
                }
                catch (...) {
                    completion.error = StoreError{
                        .code = StoreErrorCode::Unexpected,
                        .path = path_,
                        .systemCode = ERROR_UNHANDLED_EXCEPTION,
                        .message = "unexpected exception while saving config",
                    };
                }
                if (completionHandler_) {
                    completionHandler_(std::move(completion));
                }
            }
            catch (...) {
                // A background failure must not terminate the application.
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
