#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/icon_loader.h"

#include <Windows.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace {

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
