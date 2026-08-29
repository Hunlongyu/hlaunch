#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace hlaunch::platform::windows {

struct IconLoadRequest
{
    std::string itemId{};
    std::string sourceKey{};
    std::string target{};
    std::optional<std::string> icon{};
    std::uint32_t pixelSize{48};
};

struct IconLoadResult
{
    std::string itemId{};
    std::string sourceKey{};
    std::uint32_t requestedPixelSize{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels{};
    bool succeeded{};
    bool fromDiskCache{};
};

[[nodiscard]] IconLoadResult loadIconPixels(
    const IconLoadRequest &request,
    const std::filesystem::path &cacheDirectory = {});

class IconLoader final
{
  public:
    using CompletionHandler = std::function<void(IconLoadResult)>;

    explicit IconLoader(
        CompletionHandler completionHandler,
        std::filesystem::path cacheDirectory = {});
    ~IconLoader();

    IconLoader(const IconLoader &) = delete;
    IconLoader &operator=(const IconLoader &) = delete;

    void submit(IconLoadRequest request);

  private:
    void run() noexcept;

    CompletionHandler completionHandler_{};
    std::mutex mutex_{};
    std::condition_variable condition_{};
    std::deque<IconLoadRequest> pending_{};
    bool stopping_{};
    std::filesystem::path cacheDirectory_{};
    std::thread thread_{};
};

} // namespace hlaunch::platform::windows
