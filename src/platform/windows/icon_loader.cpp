#include "platform/windows/icon_loader.h"

#include "platform/windows/icon_disk_cache.h"
#include "platform/windows/shell_icon_extractor.h"

#include <Objbase.h>

#include <algorithm>
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

template <typename T>
void appendBinary(std::string &target, const T &value)
{
    target.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

std::optional<std::wstring> utf8ToWide(const std::string_view value)
{
    if (value.empty())
    {
        return std::wstring{};
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
    {
        return std::nullopt;
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), required) != required)
    {
        return std::nullopt;
    }
    return result;
}

void appendSourceMetadata(std::string &identity, const std::string_view source)
{
    const auto path = utf8ToWide(source);
    const std::uint8_t validUtf8 = path ? 1U : 0U;
    appendBinary(identity, validUtf8);
    if (!path)
    {
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    const std::uint8_t found = GetFileAttributesExW(
        path->c_str(), GetFileExInfoStandard, &attributes) ? 1U : 0U;
    appendBinary(identity, found);
    if (!found)
    {
        return;
    }
    appendBinary(identity, attributes.dwFileAttributes);
    appendBinary(identity, attributes.nFileSizeHigh);
    appendBinary(identity, attributes.nFileSizeLow);
    appendBinary(identity, attributes.ftLastWriteTime.dwHighDateTime);
    appendBinary(identity, attributes.ftLastWriteTime.dwLowDateTime);
}

std::string iconCacheIdentity(const IconLoadRequest &request)
{
    std::string identity = request.sourceKey;
    appendBinary(identity, request.pixelSize);
    const std::uint8_t hasExplicitIcon = request.icon ? 1U : 0U;
    appendBinary(identity, hasExplicitIcon);
    if (request.icon)
    {
        appendSourceMetadata(identity, *request.icon);
    }
    appendSourceMetadata(identity, request.target);
    return identity;
}

} // namespace

IconLoadResult loadIconPixels(
    const IconLoadRequest &request,
    const std::filesystem::path &cacheDirectory)
{
    IconLoadResult result{
        .itemId = request.itemId,
        .sourceKey = request.sourceKey,
        .requestedPixelSize = request.pixelSize,
    };
    const auto cacheIdentity = iconCacheIdentity(request);
    const IconDiskCache cache{{.directory = cacheDirectory}};
    if (const auto cached = cache.load(request.itemId, cacheIdentity))
    {
        result.width = cached->width;
        result.height = cached->height;
        result.pixels = cached->values;
        result.succeeded = true;
        result.fromDiskCache = true;
        return result;
    }
    auto pixels = extractShellIconPixels(request.target, request.icon, request.pixelSize);
    if (!pixels)
    {
        return result;
    }
    result.width = pixels->width;
    result.height = pixels->height;
    result.pixels = std::move(pixels->values);
    result.succeeded = true;
    static_cast<void>(cache.store(request.itemId, cacheIdentity, IconPixels{
        .width = result.width,
        .height = result.height,
        .values = result.pixels,
    }));
    return result;
}

IconLoader::IconLoader(
    CompletionHandler completionHandler,
    std::filesystem::path cacheDirectory)
    : completionHandler_(std::move(completionHandler)),
      cacheDirectory_(std::move(cacheDirectory)),
      thread_([this] { run(); })
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

void IconLoader::invalidate(std::string itemId)
{
    if (itemId.empty()) {
        return;
    }
    {
        const std::scoped_lock lock{mutex_};
        if (stopping_) {
            return;
        }
        std::erase_if(pending_, [&itemId](const IconLoadRequest& request) {
            return request.itemId == itemId;
        });
        if (std::ranges::find(invalidations_, itemId) == invalidations_.end()) {
            invalidations_.push_back(std::move(itemId));
        }
    }
    condition_.notify_one();
}

void IconLoader::run() noexcept
{
    const ComApartment apartment{};
    try {
        for (;;) {
            std::optional<IconLoadRequest> request{};
            std::string invalidatedItemId{};
            {
                std::unique_lock lock{mutex_};
                condition_.wait(lock, [this] {
                    return stopping_ || !invalidations_.empty() || !pending_.empty();
                });
                if (!invalidations_.empty()) {
                    invalidatedItemId = std::move(invalidations_.front());
                    invalidations_.pop_front();
                }
                else if (stopping_) {
                    return;
                }
                else {
                    request = std::move(pending_.front());
                    pending_.pop_front();
                }
            }

            if (!invalidatedItemId.empty()) {
                IconDiskCache{{.directory = cacheDirectory_}}.eraseScope(invalidatedItemId);
                continue;
            }

            try {
                IconLoadResult result{
                    .itemId = request->itemId,
                    .sourceKey = request->sourceKey,
                    .requestedPixelSize = request->pixelSize,
                };
                try {
                    result = loadIconPixels(*request, cacheDirectory_);
                }
                catch (...) {
                    result.succeeded = false;
                }
                if (completionHandler_) {
                    completionHandler_(std::move(result));
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
            pending_.clear();
        }
        catch (...) {
        }
    }
}

} // namespace hlaunch::platform::windows
