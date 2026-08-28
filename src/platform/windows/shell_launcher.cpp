#include "platform/windows/shell_launcher.h"

#include <shellapi.h>
#include <shlobj.h>
#include <winrt/base.h>
#include <wil/resource.h>

#include <array>
#include <optional>
#include <utility>

namespace hlaunch::platform::windows {
namespace {

std::expected<std::wstring, ShellLaunchError> utf8ToWide(const std::string_view value)
{
    if (value.empty()) {
        return std::wstring{};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::unexpected(ShellLaunchError{
            ShellLaunchErrorCode::InvalidUtf8,
            GetLastError(),
        });
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required) != required) {
        return std::unexpected(ShellLaunchError{
            ShellLaunchErrorCode::InvalidUtf8,
            GetLastError(),
        });
    }
    return result;
}

struct ShortcutLaunchData final {
    std::wstring target{};
    std::wstring parameters{};
    std::wstring workingDirectory{};
    int showCommand{SW_SHOWNORMAL};
};

std::optional<ShortcutLaunchData> tryResolveShortcut(
    const HWND owner,
    const std::wstring& shortcutPath)
{
    winrt::com_ptr<IShellLinkW> shellLink{};
    if (FAILED(CoCreateInstance(
            CLSID_ShellLink,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(shellLink.put())))) {
        return std::nullopt;
    }
    winrt::com_ptr<IPersistFile> persistFile{};
    if (FAILED(shellLink->QueryInterface(IID_PPV_ARGS(persistFile.put())))
        || FAILED(persistFile->Load(shortcutPath.c_str(), STGM_READ))) {
        return std::nullopt;
    }

    constexpr DWORD resolveFlags = SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK;
    if (FAILED(shellLink->Resolve(owner, resolveFlags))) {
        return std::nullopt;
    }

    constexpr std::size_t bufferLength = 32'768U;
    std::array<wchar_t, bufferLength> target{};
    WIN32_FIND_DATAW targetData{};
    if (FAILED(shellLink->GetPath(
            target.data(),
            static_cast<int>(target.size()),
            &targetData,
            SLGP_RAWPATH))
        || target.front() == L'\0') {
        return std::nullopt;
    }

    std::array<wchar_t, bufferLength> parameters{};
    std::array<wchar_t, bufferLength> workingDirectory{};
    static_cast<void>(shellLink->GetArguments(
        parameters.data(), static_cast<int>(parameters.size())));
    static_cast<void>(shellLink->GetWorkingDirectory(
        workingDirectory.data(), static_cast<int>(workingDirectory.size())));
    int showCommand = SW_SHOWNORMAL;
    static_cast<void>(shellLink->GetShowCmd(&showCommand));
    return ShortcutLaunchData{
        .target = target.data(),
        .parameters = parameters.data(),
        .workingDirectory = workingDirectory.data(),
        .showCommand = showCommand,
    };
}

} // namespace

std::wstring quoteWindowsArgument(const std::wstring_view argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring{argument};
    }

    std::wstring result{L'"'};
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append((backslashes * 2) + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::expected<std::wstring, ShellLaunchError> buildShellParameterString(
    const std::vector<std::string>& arguments)
{
    std::wstring parameters{};
    for (const auto& argument : arguments) {
        auto converted = utf8ToWide(argument);
        if (!converted) {
            return std::unexpected(converted.error());
        }
        if (!parameters.empty()) {
            parameters.push_back(L' ');
        }
        parameters.append(quoteWindowsArgument(*converted));
    }
    return parameters;
}

std::expected<std::wstring, ShellLaunchError> buildShellCommandLine(
    const core::LaunchItem& item)
{
    auto target = utf8ToWide(item.target);
    auto parameters = buildShellParameterString(item.arguments);
    if (!target) {
        return std::unexpected(target.error());
    }
    if (!parameters) {
        return std::unexpected(parameters.error());
    }

    std::wstring commandLine = quoteWindowsArgument(*target);
    if (!parameters->empty()) {
        commandLine.push_back(L' ');
        commandLine.append(*parameters);
    }
    return commandLine;
}

std::expected<ShellLaunchResult, ShellLaunchError> launchItem(
    const HWND owner,
    const core::LaunchItem& item)
{
    auto target = utf8ToWide(item.target);
    auto parameters = buildShellParameterString(item.arguments);
    if (!target) {
        return std::unexpected(target.error());
    }
    if (!parameters) {
        return std::unexpected(parameters.error());
    }

    std::wstring workingDirectory{};
    if (item.workingDirectory) {
        auto converted = utf8ToWide(*item.workingDirectory);
        if (!converted) {
            return std::unexpected(converted.error());
        }
        workingDirectory = std::move(*converted);
    }

    std::wstring launchTarget = std::move(*target);
    std::wstring launchParameters = std::move(*parameters);
    int showCommand = SW_SHOWNORMAL;
    if (item.type == core::ItemType::Shortcut) {
        if (auto shortcut = tryResolveShortcut(owner, launchTarget)) {
            launchTarget = std::move(shortcut->target);
            if (!shortcut->parameters.empty()) {
                if (!launchParameters.empty()) {
                    shortcut->parameters.push_back(L' ');
                    shortcut->parameters.append(launchParameters);
                }
                launchParameters = std::move(shortcut->parameters);
            }
            if (workingDirectory.empty()) {
                workingDirectory = std::move(shortcut->workingDirectory);
            }
            showCommand = shortcut->showCommand;
        }
    }

    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(SHELLEXECUTEINFOW);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    execute.hwnd = owner;
    execute.lpVerb = item.runAsAdministrator ? L"runas" : L"open";
    execute.lpFile = launchTarget.c_str();
    execute.lpParameters = launchParameters.empty() ? nullptr : launchParameters.c_str();
    execute.lpDirectory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    execute.nShow = showCommand;
    if (!ShellExecuteExW(&execute)) {
        const auto systemCode = GetLastError();
        return std::unexpected(ShellLaunchError{
            systemCode == ERROR_CANCELLED
                ? ShellLaunchErrorCode::Cancelled
                : ShellLaunchErrorCode::LaunchFailed,
            systemCode,
        });
    }
    return ShellLaunchResult{wil::unique_process_handle{execute.hProcess}};
}

std::expected<void, ShellLaunchError> openItemLocation(const core::LaunchItem& item)
{
    if (item.type == core::ItemType::Url) {
        return std::unexpected(ShellLaunchError{
            ShellLaunchErrorCode::LaunchFailed,
            ERROR_NOT_SUPPORTED,
        });
    }

    auto target = utf8ToWide(item.target);
    if (!target) {
        return std::unexpected(target.error());
    }

    PIDLIST_ABSOLUTE itemPidl{};
    const auto parseResult = SHParseDisplayName(target->c_str(), nullptr, &itemPidl, 0, nullptr);
    if (FAILED(parseResult)) {
        return std::unexpected(ShellLaunchError{
            ShellLaunchErrorCode::LaunchFailed,
            static_cast<DWORD>(parseResult),
        });
    }
    const auto itemPidlCleanup = wil::scope_exit([itemPidl] { CoTaskMemFree(itemPidl); });

    winrt::com_ptr<IShellFolder> parentFolder{};
    PCUITEMID_CHILD childPidl{};
    const auto bindResult = SHBindToParent(
        itemPidl,
        __uuidof(IShellFolder),
        parentFolder.put_void(),
        &childPidl);
    if (FAILED(bindResult)) {
        return std::unexpected(ShellLaunchError{
            ShellLaunchErrorCode::LaunchFailed,
            static_cast<DWORD>(bindResult),
        });
    }

    PIDLIST_ABSOLUTE parentPidl{ILCloneFull(itemPidl)};
    if (!parentPidl) {
        return std::unexpected(ShellLaunchError{
            ShellLaunchErrorCode::LaunchFailed,
            ERROR_NOT_ENOUGH_MEMORY,
        });
    }
    const auto parentPidlCleanup = wil::scope_exit([parentPidl] { CoTaskMemFree(parentPidl); });
    if (!ILRemoveLastID(parentPidl)) {
        return std::unexpected(ShellLaunchError{
            ShellLaunchErrorCode::LaunchFailed,
            ERROR_INVALID_NAME,
        });
    }

    const auto openResult = SHOpenFolderAndSelectItems(parentPidl, 1, &childPidl, 0);
    if (FAILED(openResult)) {
        return std::unexpected(ShellLaunchError{
            ShellLaunchErrorCode::LaunchFailed,
            static_cast<DWORD>(openResult),
        });
    }
    return {};
}

} // namespace hlaunch::platform::windows
