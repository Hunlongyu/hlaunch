#include "platform/windows/icon_disk_cache.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace hlaunch::platform::windows {
namespace {

constexpr std::array<char, 8> cacheMagic{'H', 'L', 'I', 'C', 'O', 'N', '0', '1'};
constexpr std::uint32_t cacheVersion = 4U;
constexpr std::size_t maximumIdentityBytes = 128U * 1024U;
constexpr std::uint32_t maximumDimension = 256U;
constexpr std::string_view cacheExtension = ".hlci";

std::uint64_t fnv1a(const std::string_view value, std::uint64_t hash = 14695981039346656037ULL)
{
    for (const unsigned char byte : value)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t checksum(const std::string_view identity, const IconPixels &pixels)
{
    auto hash = fnv1a(identity);
    const std::array<std::uint32_t, 2> dimensions{pixels.width, pixels.height};
    hash = fnv1a(
        std::string_view{reinterpret_cast<const char *>(dimensions.data()), sizeof(dimensions)},
        hash);
    return fnv1a(
        std::string_view{reinterpret_cast<const char *>(pixels.values.data()), pixels.values.size()},
        hash);
}

std::string scopedIdentity(
    const std::string_view scope,
    const std::string_view identity)
{
    std::string result{};
    result.reserve(scope.size() + 1U + identity.size());
    result.append(scope);
    result.push_back('\0');
    result.append(identity);
    return result;
}

std::wstring hashText(const std::string_view value)
{
    std::wostringstream text{};
    text << std::hex << std::setw(16) << std::setfill(L'0') << fnv1a(value);
    return text.str();
}

bool validPixelShape(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint64_t bytes) noexcept
{
    if (width == 0U || height == 0U
        || width > maximumDimension || height > maximumDimension)
    {
        return false;
    }
    const auto expected = static_cast<std::uint64_t>(width)
        * static_cast<std::uint64_t>(height) * 4ULL;
    return expected == bytes
        && expected <= std::numeric_limits<std::uint32_t>::max();
}

bool validPixels(const IconPixels &pixels) noexcept
{
    return validPixelShape(pixels.width, pixels.height, pixels.values.size());
}

template <typename T>
bool readValue(std::ifstream &input, T &value)
{
    input.read(reinterpret_cast<char *>(&value), sizeof(value));
    return input.good();
}

template <typename T>
bool writeValue(std::ofstream &output, const T value)
{
    output.write(reinterpret_cast<const char *>(&value), sizeof(value));
    return output.good();
}

} // namespace

IconDiskCache::IconDiskCache(IconDiskCacheOptions options)
    : options_(std::move(options))
{
}

std::filesystem::path IconDiskCache::cachePath(
    const std::string_view scope,
    const std::string_view identity) const
{
    return options_.directory
        / (hashText(scope) + L"-" + hashText(identity) + L".hlci");
}

std::optional<IconPixels> IconDiskCache::load(
    const std::string_view scope,
    const std::string_view identity) const
{
    const auto storedIdentity = scopedIdentity(scope, identity);
    if (options_.directory.empty() || scope.empty() || identity.empty()
        || storedIdentity.size() > maximumIdentityBytes)
    {
        return std::nullopt;
    }
    const auto path = cachePath(scope, identity);
    std::ifstream input{path, std::ios::binary};
    if (!input)
    {
        return std::nullopt;
    }

    std::array<char, cacheMagic.size()> magic{};
    std::uint32_t version{};
    std::uint32_t identitySize{};
    IconPixels pixels{};
    std::uint32_t pixelBytes{};
    std::uint64_t storedChecksum{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!input.good() || magic != cacheMagic
        || !readValue(input, version) || version != cacheVersion
        || !readValue(input, identitySize) || identitySize != storedIdentity.size()
        || identitySize > maximumIdentityBytes
        || !readValue(input, pixels.width)
        || !readValue(input, pixels.height)
        || !readValue(input, pixelBytes)
        || !readValue(input, storedChecksum)
        || !validPixelShape(pixels.width, pixels.height, pixelBytes))
    {
        return std::nullopt;
    }

    std::string loadedIdentity(identitySize, '\0');
    input.read(loadedIdentity.data(), static_cast<std::streamsize>(loadedIdentity.size()));
    pixels.values.resize(pixelBytes);
    input.read(
        reinterpret_cast<char *>(pixels.values.data()),
        static_cast<std::streamsize>(pixels.values.size()));
    if (!input.good() || loadedIdentity != storedIdentity || !validPixels(pixels)
        || checksum(storedIdentity, pixels) != storedChecksum)
    {
        return std::nullopt;
    }
    if (input.peek() != std::ifstream::traits_type::eof())
    {
        return std::nullopt;
    }

    std::error_code ignored{};
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ignored);
    return pixels;
}

bool IconDiskCache::store(
    const std::string_view scope,
    const std::string_view identity,
    const IconPixels &pixels) const
{
    const auto storedIdentity = scopedIdentity(scope, identity);
    if (options_.directory.empty() || scope.empty() || identity.empty()
        || storedIdentity.size() > maximumIdentityBytes || !validPixels(pixels))
    {
        return false;
    }
    std::error_code error{};
    std::filesystem::create_directories(options_.directory, error);
    if (error)
    {
        return false;
    }

    const auto path = cachePath(scope, identity);
    auto temporary = path;
    temporary += L".tmp";
    std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
    const auto identitySize = static_cast<std::uint32_t>(storedIdentity.size());
    const auto pixelBytes = static_cast<std::uint32_t>(pixels.values.size());
    const auto storedChecksum = checksum(storedIdentity, pixels);
    output.write(cacheMagic.data(), static_cast<std::streamsize>(cacheMagic.size()));
    if (!output.good()
        || !writeValue(output, cacheVersion)
        || !writeValue(output, identitySize)
        || !writeValue(output, pixels.width)
        || !writeValue(output, pixels.height)
        || !writeValue(output, pixelBytes)
        || !writeValue(output, storedChecksum))
    {
        output.close();
        std::filesystem::remove(temporary, error);
        return false;
    }
    output.write(storedIdentity.data(), static_cast<std::streamsize>(storedIdentity.size()));
    output.write(
        reinterpret_cast<const char *>(pixels.values.data()),
        static_cast<std::streamsize>(pixels.values.size()));
    output.flush();
    const bool written = output.good();
    output.close();
    if (!written || !MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, error);
        return false;
    }
    trim();
    return true;
}

void IconDiskCache::eraseScope(const std::string_view scope) const
{
    if (options_.directory.empty() || scope.empty()) {
        return;
    }
    const auto prefix = hashText(scope) + L"-";
    std::error_code error{};
    for (std::filesystem::directory_iterator iterator{options_.directory, error}, end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error)) {
            error.clear();
            continue;
        }
        const auto filename = iterator->path().filename().wstring();
        if (iterator->path().extension() == L".hlci"
            && (filename.starts_with(prefix) || filename.find(L'-') == std::wstring::npos)) {
            std::filesystem::remove(iterator->path(), error);
        }
        error.clear();
    }
}

void IconDiskCache::trim() const
{
    struct Entry
    {
        std::filesystem::path path{};
        std::filesystem::file_time_type modified{};
        std::uintmax_t bytes{};
    };
    std::vector<Entry> entries{};
    std::uintmax_t totalBytes{};
    std::error_code error{};
    for (std::filesystem::directory_iterator iterator{options_.directory, error}, end;
         !error && iterator != end;
         iterator.increment(error))
    {
        if (!iterator->is_regular_file(error)
            || iterator->path().extension().string() != cacheExtension)
        {
            error.clear();
            continue;
        }
        if (iterator->path().filename().wstring().find(L'-') == std::wstring::npos) {
            std::filesystem::remove(iterator->path(), error);
            error.clear();
            continue;
        }
        const auto bytes = iterator->file_size(error);
        if (error)
        {
            error.clear();
            continue;
        }
        const auto modified = iterator->last_write_time(error);
        if (error)
        {
            error.clear();
            continue;
        }
        entries.push_back({iterator->path(), modified, bytes});
        totalBytes += bytes;
    }
    std::ranges::sort(entries, {}, &Entry::modified);
    std::size_t remaining = entries.size();
    for (const auto &entry : entries)
    {
        if (remaining <= options_.maximumEntries && totalBytes <= options_.maximumBytes)
        {
            break;
        }
        if (std::filesystem::remove(entry.path, error))
        {
            --remaining;
            totalBytes = entry.bytes > totalBytes ? 0U : totalBytes - entry.bytes;
        }
        error.clear();
    }
}

} // namespace hlaunch::platform::windows
