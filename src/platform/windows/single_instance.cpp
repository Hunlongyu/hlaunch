#include "platform/windows/single_instance.h"

#include <Sddl.h>

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

bool notifyPrimaryInstance(const ActivationCommand command) noexcept
{
    const auto window = FindWindowW(activationWindowClassName, nullptr);
    if (!window) {
        return false;
    }

    DWORD_PTR ignoredResult = 0;
    return SendMessageTimeoutW(
               window,
               activationMessageId(),
               static_cast<WPARAM>(command),
               0,
               SMTO_ABORTIFHUNG | SMTO_BLOCK,
               1'000,
               &ignoredResult)
        != 0;
}

} // namespace hlaunch::platform::windows
