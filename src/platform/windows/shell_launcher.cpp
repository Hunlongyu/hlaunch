#include "platform/windows/shell_launcher.h"

#include <shellapi.h>

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

    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(SHELLEXECUTEINFOW);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    execute.hwnd = owner;
    execute.lpVerb = item.runAsAdministrator ? L"runas" : L"open";
    execute.lpFile = target->c_str();
    execute.lpParameters = parameters->empty() ? nullptr : parameters->c_str();
    execute.lpDirectory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    execute.nShow = SW_SHOWNORMAL;
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

} // namespace hlaunch::platform::windows
