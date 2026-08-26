#pragma once

#include "core/data_model.h"

#include <Windows.h>

#include <cstdint>
#include <expected>

namespace hlaunch::platform::windows {

struct HotkeyBinding {
    UINT modifiers{};
    UINT virtualKey{};

    bool operator==(const HotkeyBinding&) const = default;
};

enum class HotkeyErrorCode : std::uint8_t {
    InvalidConfiguration,
    RegistrationFailed,
    UnregistrationFailed,
};

struct HotkeyError {
    HotkeyErrorCode code{HotkeyErrorCode::InvalidConfiguration};
    DWORD systemCode{};
};

[[nodiscard]] std::expected<HotkeyBinding, HotkeyError> resolveHotkeyBinding(
    const core::HotkeyConfig& config) noexcept;

class GlobalHotkey final {
public:
    GlobalHotkey() = default;
    ~GlobalHotkey();

    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;
    GlobalHotkey(GlobalHotkey&&) = delete;
    GlobalHotkey& operator=(GlobalHotkey&&) = delete;

    [[nodiscard]] std::expected<void, HotkeyError> apply(
        HWND owner,
        const core::HotkeyConfig& config);
    void unregister() noexcept;

    [[nodiscard]] bool isRegistered() const noexcept;
    [[nodiscard]] bool handlesMessage(WPARAM hotkeyId) const noexcept;
    [[nodiscard]] const HotkeyBinding* binding() const noexcept;

private:
    static constexpr int primaryId = 0x484C;
    static constexpr int replacementId = 0x484D;

    HWND owner_{};
    int registeredId_{};
    HotkeyBinding binding_{};
};

} // namespace hlaunch::platform::windows
