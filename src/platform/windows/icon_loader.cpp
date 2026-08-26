#include "platform/windows/icon_loader.h"

#include "platform/windows/shell_icon_extractor.h"

#include <Objbase.h>

#include <utility>

namespace hlaunch::platform::windows {
namespace {

class ComApartment final
{
  public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment()
    {
        if (SUCCEEDED(result_))
        {
            CoUninitialize();
        }
    }

  private:
    HRESULT result_{};
};

} // namespace

IconLoadResult loadIconPixels(const IconLoadRequest &request)
{
    IconLoadResult result{
        .itemId = request.itemId,
        .sourceKey = request.sourceKey,
        .requestedPixelSize = request.pixelSize,
    };
    auto pixels = extractShellIconPixels(request.target, request.icon, request.pixelSize);
    if (!pixels)
    {
        return result;
    }
    result.width = pixels->width;
    result.height = pixels->height;
    result.pixels = std::move(pixels->values);
    result.succeeded = true;
    return result;
}

IconLoader::IconLoader(CompletionHandler completionHandler)
    : completionHandler_(std::move(completionHandler)), thread_([this] { run(); })
{
}

IconLoader::~IconLoader()
{
    {
        const std::scoped_lock lock{mutex_};
        stopping_ = true;
        pending_.clear();
    }
    condition_.notify_one();
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void IconLoader::submit(IconLoadRequest request)
{
    {
        const std::scoped_lock lock{mutex_};
        if (stopping_)
        {
            return;
        }
        pending_.push_back(std::move(request));
    }
    condition_.notify_one();
}

void IconLoader::run()
{
    const ComApartment apartment{};
    for (;;)
    {
        IconLoadRequest request{};
        {
            std::unique_lock lock{mutex_};
            condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_)
            {
                return;
            }
            request = std::move(pending_.front());
            pending_.pop_front();
        }

        IconLoadResult result{
            .itemId = request.itemId,
            .sourceKey = request.sourceKey,
            .requestedPixelSize = request.pixelSize,
        };
        try
        {
            result = loadIconPixels(request);
        }
        catch (...)
        {
            result.succeeded = false;
        }
        if (completionHandler_)
        {
            completionHandler_(std::move(result));
        }
    }
}

} // namespace hlaunch::platform::windows
