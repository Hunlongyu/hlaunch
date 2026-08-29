#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/icon_disk_cache.h"
#include "platform/windows/icon_loader.h"
#include "platform/windows/svg_icon_renderer.h"

#include <Windows.h>
#include <doctest/doctest.h>
#include <wil/resource.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>

namespace {

class TemporaryDirectory final
{
  public:
    TemporaryDirectory()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / (L"hlaunch-icon-cache-" + std::to_wstring(GetCurrentProcessId())
               + L"-" + std::to_wstring(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored{};
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

  private:
    std::filesystem::path path_{};
};

std::string currentExecutableUtf8()
{
    std::wstring path(32'768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    REQUIRE(length > 0);
    path.resize(length);
    const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
                                               static_cast<int>(path.size()), nullptr, 0,
                                               nullptr, nullptr);
    REQUIRE(required > 0);
    std::string result(static_cast<std::size_t>(required), '\0');
    REQUIRE(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
                                static_cast<int>(path.size()), result.data(), required,
                                nullptr, nullptr) == required);
    return result;
}

bool isPremultiplied(const std::vector<std::uint8_t>& pixels)
{
    if (pixels.size() % 4U != 0U) {
        return false;
    }
    for (std::size_t index = 0; index < pixels.size(); index += 4U) {
        const auto alpha = pixels[index + 3U];
        if (pixels[index] > alpha
            || pixels[index + 1U] > alpha
            || pixels[index + 2U] > alpha) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("UI-ICON-001 extracts premultiplied pixels for an executable")
{
    const auto result = hlaunch::platform::windows::loadIconPixels({
        .itemId = "item",
        .sourceKey = "source",
        .target = currentExecutableUtf8(),
        .pixelSize = 48,
    });

    REQUIRE(result.succeeded);
    CHECK(result.itemId == "item");
    CHECK(result.sourceKey == "source");
    CHECK(result.width == 48U);
    CHECK(result.height == 48U);
    REQUIRE(result.pixels.size() == 48U * 48U * 4U);
    bool hasVisiblePixel{};
    for (std::size_t index = 3; index < result.pixels.size(); index += 4U)
    {
        hasVisiblePixel = hasVisiblePixel || result.pixels[index] != 0;
    }
    CHECK(hasVisiblePixel);
    CHECK(isPremultiplied(result.pixels));
}

TEST_CASE("UI-ICON-001 premultiplies transparent Shell-style BGRA before scaling")
{
    BITMAPINFO information{};
    information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    information.bmiHeader.biWidth = 2;
    information.bmiHeader.biHeight = -2;
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    void* rawPixels{};
    wil::unique_hbitmap bitmap{CreateDIBSection(
        nullptr,
        &information,
        DIB_RGB_COLORS,
        &rawPixels,
        nullptr,
        0)};
    REQUIRE(bitmap);
    REQUIRE(rawPixels != nullptr);
    const std::array<std::uint8_t, 16> straightAlphaPixels{
        255, 255, 255, 0,
        0, 255, 0, 128,
        0, 0, 255, 255,
        255, 0, 0, 255,
    };
    std::copy(
        straightAlphaPixels.begin(),
        straightAlphaPixels.end(),
        static_cast<std::uint8_t*>(rawPixels));

    const auto converted = hlaunch::platform::windows::convertBitmapToPixels(
        bitmap.get(), 32U);
    REQUIRE(converted.has_value());
    CHECK(converted->width == 32U);
    CHECK(converted->height == 32U);
    CHECK(isPremultiplied(converted->values));
    for (std::size_t index = 0; index < converted->values.size(); index += 4U) {
        if (converted->values[index + 3U] == 0U) {
            CHECK(converted->values[index] == 0U);
            CHECK(converted->values[index + 1U] == 0U);
            CHECK(converted->values[index + 2U] == 0U);
        }
    }
}

TEST_CASE("UI-ICON-001 decodes ICO directly at mixed-DPI physical sizes")
{
    const auto iconPath = std::filesystem::path{HLAUNCH_TEST_SOURCE_DIR}
        / L"resources" / L"branding" / L"hlaunch-logo.ico";
    REQUIRE(std::filesystem::is_regular_file(iconPath));
    for (const auto size : {32U, 40U, 48U, 56U, 64U}) {
        const auto pixels = hlaunch::platform::windows::decodeImageFileToPixels(
            iconPath.wstring(), size);
        REQUIRE(pixels.has_value());
        CHECK(pixels->width == size);
        CHECK(pixels->height == size);
        CHECK(pixels->values.size() == static_cast<std::size_t>(size) * size * 4U);
        CHECK(isPremultiplied(pixels->values));
    }
}

TEST_CASE("UI-ICON-001 renders SVG directly at mixed-DPI physical sizes")
{
    const auto svgPath = std::filesystem::path{HLAUNCH_TEST_SOURCE_DIR}
        / L"tests" / L"assets" / L"icon-test.svg";
    REQUIRE(std::filesystem::is_regular_file(svgPath));
    for (const auto size : {32U, 40U, 48U, 56U, 64U}) {
        const auto pixels = hlaunch::platform::windows::decodeSvgFileToPixels(
            svgPath.wstring(), size);
        REQUIRE(pixels.has_value());
        CHECK(pixels->width == size);
        CHECK(pixels->height == size);
        CHECK(pixels->values.size() == static_cast<std::size_t>(size) * size * 4U);
        CHECK(isPremultiplied(pixels->values));
        CHECK(std::ranges::any_of(
            pixels->values | std::views::drop(3) | std::views::stride(4),
            [](const std::uint8_t alpha) { return alpha != 0U; }));
    }
}

TEST_CASE("UI-ICON-001 background loader preserves request identity")
{
    std::mutex mutex{};
    std::condition_variable condition{};
    std::optional<hlaunch::platform::windows::IconLoadResult> completed{};
    hlaunch::platform::windows::IconLoader loader{
        [&](hlaunch::platform::windows::IconLoadResult result) {
            {
                const std::scoped_lock lock{mutex};
                completed = std::move(result);
            }
            condition.notify_one();
        }};

    loader.submit({
        .itemId = "async-item",
        .sourceKey = "async-source",
        .target = currentExecutableUtf8(),
        .pixelSize = 32,
    });

    std::unique_lock lock{mutex};
    REQUIRE(condition.wait_for(lock, std::chrono::seconds{5}, [&] { return completed.has_value(); }));
    REQUIRE(completed->succeeded);
    CHECK(completed->itemId == "async-item");
    CHECK(completed->sourceKey == "async-source");
    CHECK(completed->requestedPixelSize == 32U);
}

TEST_CASE("UI-ICON-001 a throwing completion callback cannot terminate the loader")
{
    std::mutex mutex{};
    std::condition_variable condition{};
    bool callbackReached{};
    hlaunch::platform::windows::IconLoader loader{
        [&](hlaunch::platform::windows::IconLoadResult) {
            {
                const std::scoped_lock lock{mutex};
                callbackReached = true;
            }
            condition.notify_one();
            throw std::runtime_error{"test completion failure"};
        }};

    loader.submit({
        .itemId = "throwing-callback-item",
        .sourceKey = "throwing-callback-source",
        .target = currentExecutableUtf8(),
        .pixelSize = 32,
    });

    std::unique_lock lock{mutex};
    REQUIRE(condition.wait_for(
        lock, std::chrono::seconds{5}, [&] { return callbackReached; }));
}

TEST_CASE("UI-ICON-001 disk cache survives reload and repairs corrupt entries")
{
    TemporaryDirectory temporary{};
    const hlaunch::platform::windows::IconLoadRequest request{
        .itemId = "cached-item",
        .sourceKey = "cached-source",
        .target = currentExecutableUtf8(),
        .pixelSize = 32,
    };

    const auto first = hlaunch::platform::windows::loadIconPixels(request, temporary.path());
    REQUIRE(first.succeeded);
    CHECK_FALSE(first.fromDiskCache);
    const auto second = hlaunch::platform::windows::loadIconPixels(request, temporary.path());
    REQUIRE(second.succeeded);
    CHECK(second.fromDiskCache);
    CHECK(second.pixels == first.pixels);

    std::filesystem::path cacheFile{};
    for (const auto &entry : std::filesystem::directory_iterator{temporary.path()})
    {
        if (entry.path().extension() == L".hlci")
        {
            cacheFile = entry.path();
            break;
        }
    }
    REQUIRE_FALSE(cacheFile.empty());
    {
        std::ofstream corrupt{cacheFile, std::ios::binary | std::ios::trunc};
        REQUIRE(corrupt.good());
        corrupt.write("bad", 3);
    }

    const auto repaired = hlaunch::platform::windows::loadIconPixels(request, temporary.path());
    REQUIRE(repaired.succeeded);
    CHECK_FALSE(repaired.fromDiskCache);
    const auto reloaded = hlaunch::platform::windows::loadIconPixels(request, temporary.path());
    REQUIRE(reloaded.succeeded);
    CHECK(reloaded.fromDiskCache);
}

TEST_CASE("UI-ICON-001 disk cache keeps independent mixed-DPI variants")
{
    TemporaryDirectory temporary{};
    const auto makeRequest = [](const std::uint32_t size) {
        return hlaunch::platform::windows::IconLoadRequest{
            .itemId = "multi-dpi-item",
            .sourceKey = "multi-dpi-source",
            .target = currentExecutableUtf8(),
            .pixelSize = size,
        };
    };

    const auto first32 = hlaunch::platform::windows::loadIconPixels(
        makeRequest(32U), temporary.path());
    const auto first64 = hlaunch::platform::windows::loadIconPixels(
        makeRequest(64U), temporary.path());
    REQUIRE(first32.succeeded);
    REQUIRE(first64.succeeded);
    CHECK_FALSE(first32.fromDiskCache);
    CHECK_FALSE(first64.fromDiskCache);
    CHECK(first32.width == 32U);
    CHECK(first64.width == 64U);

    const auto second32 = hlaunch::platform::windows::loadIconPixels(
        makeRequest(32U), temporary.path());
    const auto second64 = hlaunch::platform::windows::loadIconPixels(
        makeRequest(64U), temporary.path());
    REQUIRE(second32.succeeded);
    REQUIRE(second64.succeeded);
    CHECK(second32.fromDiskCache);
    CHECK(second64.fromDiskCache);
}

TEST_CASE("UI-ICON-001 disk cache trims only its oldest bounded entries")
{
    TemporaryDirectory temporary{};
    const auto unrelated = temporary.path() / L"keep.txt";
    {
        std::ofstream output{unrelated};
        output << "keep";
    }
    const hlaunch::platform::windows::IconDiskCache cache{{
        .directory = temporary.path(),
        .maximumEntries = 2,
        .maximumBytes = 1U * 1024U * 1024U,
    }};
    const hlaunch::platform::windows::IconPixels pixels{
        .width = 1,
        .height = 1,
        .values = {1, 2, 3, 4},
    };

    REQUIRE(cache.store("one", pixels));
    REQUIRE(cache.store("two", pixels));
    REQUIRE(cache.store("three", pixels));

    std::size_t cacheEntryCount{};
    for (const auto &entry : std::filesystem::directory_iterator{temporary.path()})
    {
        cacheEntryCount += entry.path().extension() == L".hlci" ? 1U : 0U;
    }
    CHECK(cacheEntryCount == 2U);
    CHECK(std::filesystem::is_regular_file(unrelated));
}
