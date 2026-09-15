#include "platform/windows/shortcut_importer.h"

#include "platform/windows/item_path_policy.h"
#include "platform/windows/uuid.h"

#include <Windows.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <propsys.h>
#include <propkey.h>
#include <winrt/base.h>
#include <wil/resource.h>

#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace hlaunch::platform::windows {
namespace {

std::optional<std::string> toUtf8(const std::wstring_view value)
{
    if (value.empty()) {
        return std::string{};
    }
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::nullopt;
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), text.data(), size, nullptr, nullptr) != size) {
        return std::nullopt;
    }
    return text;
}

std::optional<std::vector<char>> readLink(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto size = stream.tellg();
    // Shell links are small metadata files; bound private storage per import.
    if (!stream || size <= 0 || size > 1024 * 1024) {
        return std::nullopt;
    }
    std::vector<char> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        return std::nullopt;
    }
    return bytes;
}

std::optional<std::string> retainLink(
    const std::filesystem::path& source,
    const std::filesystem::path& directory)
{
    if (!directory.is_absolute()) {
        return std::nullopt;
    }
    const auto bytes = readLink(source);
    if (!bytes) {
        return std::nullopt;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return std::nullopt;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : *bytes) {
        hash = (hash ^ static_cast<unsigned char>(byte)) * 1099511628211ULL;
    }
    auto destination = directory / std::format("{:016x}.lnk", hash);
    // The hash is only an index: verify the entire snapshot before reusing it.
    if (const auto existing = readLink(destination)) {
        if (*existing == *bytes) {
            return toUtf8(destination.wstring());
        }
        const auto id = createUuidV4();
        if (!id) {
            return std::nullopt;
        }
        destination = directory / (*id + ".lnk");
    }
    const auto target = toUtf8(destination.wstring());
    if (!target) {
        return std::nullopt;
    }
    wil::unique_hfile file{CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) {
        // Another importer may have completed the same snapshot concurrently.
        const auto existing = readLink(destination);
        return existing && *existing == *bytes ? target : std::nullopt;
    }
    bool saved = false;
    const auto cleanup = wil::scope_exit([&] {
        file.reset();
        if (!saved) {
            DeleteFileW(destination.c_str());
        }
    });
    DWORD written{};
    if (!WriteFile(file.get(), bytes->data(), static_cast<DWORD>(bytes->size()), &written, nullptr)
        || written != bytes->size() || !FlushFileBuffers(file.get())) {
        return std::nullopt;
    }
    saved = true;
    return target;
}

std::optional<core::LaunchItem> flattenLink(
    IShellLinkW* link, const core::LaunchItem& original,
    const std::filesystem::path& source)
{
    winrt::com_ptr<IShellLinkDataList> data;
    DWORD flags{};
    int showCommand{};
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(data.put())))
        || FAILED(data->GetFlags(&flags)) || FAILED(link->GetShowCmd(&showCommand))
        || showCommand != SW_SHOWNORMAL) {
        return std::nullopt;
    }
    constexpr DWORD plainFlags = SLDF_HAS_ID_LIST | SLDF_HAS_LINK_INFO | SLDF_HAS_NAME
        | SLDF_HAS_RELPATH | SLDF_HAS_WORKINGDIR | SLDF_HAS_ARGS | SLDF_HAS_ICONLOCATION
        | SLDF_UNICODE | SLDF_HAS_EXP_SZ | SLDF_HAS_EXP_ICON_SZ | SLDF_RUNAS_USER;
    if ((flags & ~plainFlags) != 0) {
        return std::nullopt;
    }
    // Windows adds volume and creator-SID metadata even to ordinary links.
    // These describe the source, not activation; retain all other properties.
    constexpr PROPERTYKEY creatorSid{
        {0x46588ae2, 0x4cbc, 0x4338, {0xbb, 0xfc, 0x13, 0x93, 0x26, 0x98, 0x6d, 0xce}}, 4};
    winrt::com_ptr<IPropertyStore> properties;
    DWORD propertyCount{};
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(properties.put())))
        || FAILED(properties->GetCount(&propertyCount))) {
        return std::nullopt;
    }
    for (DWORD index = 0; index < propertyCount; ++index) {
        PROPERTYKEY key{};
        if (FAILED(properties->GetAt(index, &key))
            || (!IsEqualPropertyKey(key, PKEY_VolumeId) && !IsEqualPropertyKey(key, creatorSid))) {
            return std::nullopt;
        }
    }
    constexpr auto resolveFlags = SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK;
    if (FAILED(link->Resolve(nullptr, resolveFlags))) {
        return std::nullopt;
    }
    std::array<wchar_t, 32768> target{}, arguments{}, workingDirectory{}, icon{};
    int iconIndex{};
    if (FAILED(link->GetPath(target.data(), static_cast<int>(target.size()), nullptr, SLGP_RAWPATH))
        || target.front() == L'\0'
        || FAILED(link->GetArguments(arguments.data(), static_cast<int>(arguments.size())))
        || FAILED(link->GetWorkingDirectory(workingDirectory.data(), static_cast<int>(workingDirectory.size())))
        || FAILED(link->GetIconLocation(icon.data(), static_cast<int>(icon.size()), &iconIndex))
        || iconIndex != 0) {
        return std::nullopt;
    }
    auto item = original;
    const auto targetText = toUtf8(target.data());
    const auto directoryText = toUtf8(workingDirectory.data());
    const auto iconText = toUtf8(icon.data());
    if (!targetText || !directoryText || !iconText) {
        return std::nullopt;
    }
    item.target = *targetText;
    if (!directoryText->empty()) {
        item.workingDirectory = *directoryText;
    }
    if (!iconText->empty()) {
        item.icon = *iconText;
    }
    if (arguments.front() != L'\0') {
        // The dummy executable prevents CommandLineToArgvW's special argv[0] rules
        // from changing a leading quoted argument, empty argument or backslash.
        const auto command = std::wstring{L"hlaunch.exe "} + arguments.data();
        int count{};
        auto argv = CommandLineToArgvW(command.c_str(), &count);
        if (!argv) {
            return std::nullopt;
        }
        const auto freeArguments = wil::scope_exit([argv] { LocalFree(argv); });
        for (int index = 1; index < count; ++index) {
            const auto argument = toUtf8(argv[index]);
            if (!argument) {
                return std::nullopt;
            }
            item.arguments.push_back(*argument);
        }
    }
    item.runAsAdministrator = (flags & SLDF_RUNAS_USER) != 0;
    auto resolved = resolveItemPaths(item, {.executableDirectory = source.parent_path()});
    if (!resolved) {
        return std::nullopt;
    }
    const std::filesystem::path targetPath{
        std::u8string{resolved->target.begin(), resolved->target.end()}};
    const auto attributes = GetFileAttributesW(targetPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return std::nullopt;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        resolved->type = core::ItemType::Folder;
    }
    else if (_wcsicmp(targetPath.extension().c_str(), L".exe") == 0) {
        resolved->type = core::ItemType::Application;
    }
    else if (_wcsicmp(targetPath.extension().c_str(), L".lnk") == 0) {
        // Retain Shell behavior for chained links instead of depending on another link.
        return std::nullopt;
    }
    else {
        resolved->type = core::ItemType::File;
    }
    return *resolved;
}

} // namespace

std::optional<core::LaunchItem> importShortcut(
    core::LaunchItem item, const std::filesystem::path& source,
    const std::filesystem::path& shortcutDirectory)
{
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto cleanup = wil::scope_exit([apartment] {
        if (SUCCEEDED(apartment)) {
            CoUninitialize();
        }
    });
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
        return std::nullopt;
    }
    winrt::com_ptr<IShellLinkW> link;
    winrt::com_ptr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(link.put())))
        || FAILED(link->QueryInterface(IID_PPV_ARGS(file.put())))
        || FAILED(file->Load(source.c_str(), STGM_READ))) {
        return std::nullopt;
    }
    if (auto flattened = flattenLink(link.get(), item, source)) {
        return flattened;
    }
    const auto retained = retainLink(source, shortcutDirectory);
    if (!retained) {
        return std::nullopt;
    }
    item.target = *retained;
    return item;
}

} // namespace hlaunch::platform::windows
