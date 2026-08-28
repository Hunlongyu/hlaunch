#include "platform/windows/single_instance.h"

#include <Sddl.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace hlaunch::platform::windows {
namespace {

std::expected<std::wstring, InstanceError> currentUserSid()
{
    wil::unique_handle processToken{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, processToken.put())) {
        return std::unexpected(InstanceError{GetLastError(), "failed to open the process token"});
    }

    DWORD requiredBytes = 0;
    GetTokenInformation(processToken.get(), TokenUser, nullptr, 0, &requiredBytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredBytes == 0) {
        return std::unexpected(InstanceError{GetLastError(), "failed to size the user token"});
    }

    std::vector<std::byte> tokenBuffer(requiredBytes);
    if (!GetTokenInformation(
            processToken.get(),
            TokenUser,
            tokenBuffer.data(),
            requiredBytes,
            &requiredBytes)) {
        return std::unexpected(InstanceError{GetLastError(), "failed to read the user token"});
    }

    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(tokenBuffer.data());
    wil::unique_hlocal_string sidString{};
    if (!ConvertSidToStringSidW(tokenUser->User.Sid, sidString.put())) {
        return std::unexpected(InstanceError{GetLastError(), "failed to format the user SID"});
    }
    return std::wstring{sidString.get()};
}

} // namespace

SingleInstance::SingleInstance(wil::unique_handle mutex, const bool primary) noexcept
    : mutex_(std::move(mutex))
    , primary_(primary)
{
}

std::expected<SingleInstance, InstanceError> SingleInstance::acquire()
{
    auto sid = currentUserSid();
    if (!sid) {
        return std::unexpected(std::move(sid.error()));
    }
    const auto mutexName = L"Local\\HLaunch.Singleton.v1." + *sid;
    wil::unique_handle mutex{CreateMutexW(nullptr, FALSE, mutexName.c_str())};
    if (!mutex) {
        return std::unexpected(InstanceError{GetLastError(), "failed to create the instance mutex"});
    }
    const bool primary = GetLastError() != ERROR_ALREADY_EXISTS;
    return SingleInstance{std::move(mutex), primary};
}

bool SingleInstance::isPrimary() const noexcept
{
    return primary_;
}

UINT activationMessageId() noexcept
{
    static const UINT message = RegisterWindowMessageW(activationMessageName);
    return message;
}

std::expected<void, PrimaryNotificationError> notifyPrimaryInstance(
    const ActivationCommand command,
    const PrimaryNotificationOptions& options) noexcept
{
    HWND window{};
    const auto attempts = std::max(1U, options.findAttempts);
    for (unsigned int attempt = 0; attempt < attempts; ++attempt) {
        window = FindWindowW(activationWindowClassName, nullptr);
        if (window) {
            break;
        }
        if (attempt + 1U < attempts && options.retryDelayMilliseconds > 0U) {
            Sleep(options.retryDelayMilliseconds);
        }
    }
    if (!window) {
        return std::unexpected(PrimaryNotificationError{
            PrimaryNotificationErrorCode::WindowNotFound,
            ERROR_FILE_NOT_FOUND,
        });
    }

    DWORD_PTR ignoredResult = 0;
    SetLastError(ERROR_SUCCESS);
    if (SendMessageTimeoutW(
            window,
            activationMessageId(),
            static_cast<WPARAM>(command),
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            options.sendTimeoutMilliseconds,
            &ignoredResult)
        == 0) {
        const auto code = GetLastError();
        return std::unexpected(PrimaryNotificationError{
            PrimaryNotificationErrorCode::DeliveryFailed,
            code == ERROR_SUCCESS ? ERROR_TIMEOUT : code,
        });
    }
    return {};
}

} // namespace hlaunch::platform::windows
