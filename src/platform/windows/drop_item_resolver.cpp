#include "platform/windows/drop_item_resolver.h"

#include <Shlwapi.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hlaunch::platform::windows {
namespace {

std::optional<std::string> wideToUtf8(const std::wstring_view value)
{
    if (value.empty())
    {
        return std::string{};
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0, nullptr,
                                         nullptr);
    if (size <= 0)
    {
        return std::nullopt;
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size, nullptr,
                            nullptr) != size)
    {
        return std::nullopt;
    }
    return result;
}

std::wstring lower(std::wstring value)
{
    std::ranges::transform(value, value.begin(),
                           [](const wchar_t character) { return std::towlower(character); });
    return value;
}

std::wstring shortenedName(std::wstring value)
{
    constexpr std::size_t maximumCodeUnits = 60;
    if (value.size() <= maximumCodeUnits)
    {
        return value;
    }
    std::size_t length = maximumCodeUnits;
    if (length > 0 && IS_HIGH_SURROGATE(value[length - 1U]) &&
        IS_LOW_SURROGATE(value[length]))
    {
        --length;
    }
    value.resize(length);
    return value;
}

std::optional<core::LaunchItem> resolvePath(const std::wstring &value)
{
    const DWORD attributes = GetFileAttributesW(value.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return std::nullopt;
    }

    const std::filesystem::path path{value};
    const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const auto extension = lower(path.extension().wstring());
    core::ItemType type = core::ItemType::File;
    if (directory)
    {
        type = core::ItemType::Folder;
    }
    else if (extension == L".exe")
    {
        type = core::ItemType::Application;
    }
    else if (extension == L".lnk")
    {
        type = core::ItemType::Shortcut;
    }

    std::wstring displayName = directory || type == core::ItemType::File
        ? path.filename().wstring()
        : path.stem().wstring();
    if (displayName.empty())
    {
        displayName = value;
    }
    const auto name = wideToUtf8(shortenedName(std::move(displayName)));
    const auto target = wideToUtf8(value);
    if (!name || name->empty() || !target || target->empty())
    {
        return std::nullopt;
    }
    return core::LaunchItem{
        .type = type,
        .name = *name,
        .target = *target,
    };
}

bool hasBlockedScheme(const std::wstring &value)
{
    const auto separator = value.find(L':');
    if (separator == std::wstring::npos)
    {
        return true;
    }
    const auto scheme = lower(value.substr(0, separator));
    return scheme == L"javascript" || scheme == L"vbscript" || scheme == L"data";
}

std::optional<core::LaunchItem> resolveUrl(const std::wstring &value)
{
    if (!PathIsURLW(value.c_str()) || hasBlockedScheme(value))
    {
        return std::nullopt;
    }

    std::wstring displayName{};
    std::array<wchar_t, 256> host{};
    DWORD hostLength = static_cast<DWORD>(host.size());
    if (SUCCEEDED(UrlGetPartW(value.c_str(), host.data(), &hostLength, URL_PART_HOSTNAME, 0)) &&
        hostLength > 0)
    {
        displayName.assign(host.data(), hostLength);
    }
    if (displayName.empty())
    {
        displayName = value;
    }

    const auto name = wideToUtf8(shortenedName(std::move(displayName)));
    const auto target = wideToUtf8(value);
    if (!name || name->empty() || !target || target->empty())
    {
        return std::nullopt;
    }
    return core::LaunchItem{
        .type = core::ItemType::Url,
        .name = *name,
        .target = *target,
    };
}

} // namespace

DropImportResult resolveDroppedSources(const DropImportRequest &request)
{
    DropImportResult result{
        .targetTabIndex = request.targetTabIndex,
        .targetGridSlot = request.targetGridSlot,
    };
    result.items.reserve(request.sources.size());
    for (const auto &source : request.sources)
    {
        auto item = source.kind == DroppedSourceKind::Path ? resolvePath(source.value)
                                                           : resolveUrl(source.value);
        if (!item)
        {
            ++result.unsupportedCount;
            continue;
        }
        result.items.push_back(std::move(*item));
    }
    return result;
}

std::optional<core::LaunchItem> makeDropLaunchItem(
    core::LaunchItem item,
    const std::vector<DroppedSource>& sources)
{
    bool appendedPath{};
    for (const auto& source : sources)
    {
        if (source.kind != DroppedSourceKind::Path)
        {
            continue;
        }
        const auto argument = wideToUtf8(source.value);
        if (!argument || argument->empty())
        {
            return std::nullopt;
        }
        item.arguments.push_back(std::move(*argument));
        appendedPath = true;
    }
    return appendedPath ? std::optional<core::LaunchItem>{std::move(item)} : std::nullopt;
}

DropItemResolver::DropItemResolver(CompletionHandler completionHandler)
    : completionHandler_(std::move(completionHandler)), thread_([this] { run(); })
{
}

DropItemResolver::~DropItemResolver()
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

void DropItemResolver::submit(DropImportRequest request)
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

void DropItemResolver::run()
{
    for (;;)
    {
        DropImportRequest request{};
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

        DropImportResult result{};
        try
        {
            result = resolveDroppedSources(request);
        }
        catch (...)
        {
            result.targetTabIndex = request.targetTabIndex;
            result.targetGridSlot = request.targetGridSlot;
            result.unsupportedCount = request.sources.size();
            result.failed = true;
        }
        if (completionHandler_)
        {
            completionHandler_(std::move(result));
        }
    }
}

} // namespace hlaunch::platform::windows
