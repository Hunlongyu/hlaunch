#pragma once

#include "platform/windows/activation_command.h"

#include <Windows.h>
#include <wil/resource.h>

#include <expected>
#include <string>

namespace hlaunch::platform::windows {

inline constexpr wchar_t activationWindowClassName[] = L"HLaunch.ActivationWindow.v1";
inline constexpr wchar_t activationMessageName[] = L"HLaunch.ActivationCommand.v1";

struct InstanceError {
    unsigned long systemCode{};
    std::string message{};
};

enum class PrimaryNotificationErrorCode {
    WindowNotFound,
    DeliveryFailed,
};

struct PrimaryNotificationError {
    PrimaryNotificationErrorCode code{PrimaryNotificationErrorCode::WindowNotFound};
    unsigned long systemCode{};
};

struct PrimaryNotificationOptions {
    unsigned int findAttempts{20U};
    unsigned long retryDelayMilliseconds{50U};
    unsigned long sendTimeoutMilliseconds{1'000U};
};

class SingleInstance final {
public:
    [[nodiscard]] static std::expected<SingleInstance, InstanceError> acquire();

    SingleInstance(SingleInstance&&) noexcept = default;
    SingleInstance& operator=(SingleInstance&&) noexcept = default;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    [[nodiscard]] bool isPrimary() const noexcept;

private:
    SingleInstance(wil::unique_handle mutex, bool primary) noexcept;

    wil::unique_handle mutex_{};
    bool primary_{};
};

[[nodiscard]] UINT activationMessageId() noexcept;
[[nodiscard]] std::expected<void, PrimaryNotificationError> notifyPrimaryInstance(
    ActivationCommand command,
    const PrimaryNotificationOptions& options = {}) noexcept;

} // namespace hlaunch::platform::windows
