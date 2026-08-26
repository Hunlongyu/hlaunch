#include "platform/windows/global_hotkey.h"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace hlaunch::platform::windows {
namespace {

std::expected<UINT, HotkeyError> resolveVirtualKey(const std::string_view key) noexcept
{
    if (key.size() == 1) {
        const auto character = key.front();
        if ((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')) {
            return static_cast<UINT>(character);
        }
    }

    struct NamedKey {
        std::string_view name;
        UINT virtualKey;
    };
    constexpr NamedKey namedKeys[]{
        {"Space", VK_SPACE},
        {"Enter", VK_RETURN},
        {"Escape", VK_ESCAPE},
        {"Tab", VK_TAB},
        {"Backspace", VK_BACK},
        {"Delete", VK_DELETE},
        {"Insert", VK_INSERT},
        {"Home", VK_HOME},
        {"End", VK_END},
        {"PageUp", VK_PRIOR},
        {"PageDown", VK_NEXT},
        {"Left", VK_LEFT},
        {"Right", VK_RIGHT},
        {"Up", VK_UP},
        {"Down", VK_DOWN},
    };
    const auto named = std::ranges::find(namedKeys, key, &NamedKey::name);
    if (named != std::ranges::end(namedKeys)) {
        return named->virtualKey;
    }

    if (key.size() >= 2 && key.front() == 'F') {
        unsigned functionNumber{};
        const auto* begin = key.data() + 1;
        const auto* end = key.data() + key.size();
        const auto parsed = std::from_chars(begin, end, functionNumber);
        if (parsed.ec == std::errc{} && parsed.ptr == end
            && functionNumber >= 1 && functionNumber <= 24 && functionNumber != 12) {
            return static_cast<UINT>(VK_F1 + functionNumber - 1);
        }
    }

    return std::unexpected(HotkeyError{HotkeyErrorCode::InvalidConfiguration});
}

} // namespace

std::expected<HotkeyBinding, HotkeyError> resolveHotkeyBinding(
    const core::HotkeyConfig& config) noexcept
{
    if (!config.enabled || config.behavior != core::HotkeyBehavior::Toggle
        || config.modifiers.empty()) {
        return std::unexpected(HotkeyError{HotkeyErrorCode::InvalidConfiguration});
    }

    UINT modifiers{};
    for (const auto modifier : config.modifiers) {
        UINT flag{};
        switch (modifier) {
        case core::HotkeyModifier::Alt:
            flag = MOD_ALT;
            break;
        case core::HotkeyModifier::Control:
            flag = MOD_CONTROL;
            break;
        case core::HotkeyModifier::Shift:
            flag = MOD_SHIFT;
            break;
        case core::HotkeyModifier::Win:
            flag = MOD_WIN;
            break;
        }
        if ((modifiers & flag) != 0) {
            return std::unexpected(HotkeyError{HotkeyErrorCode::InvalidConfiguration});
        }
        modifiers |= flag;
    }

    auto virtualKey = resolveVirtualKey(config.key);
    if (!virtualKey) {
        return std::unexpected(virtualKey.error());
    }
    return HotkeyBinding{modifiers, *virtualKey};
}

GlobalHotkey::~GlobalHotkey()
{
    unregister();
}

std::expected<void, HotkeyError> GlobalHotkey::apply(
    const HWND owner,
    const core::HotkeyConfig& config)
{
    if (!config.enabled) {
        unregister();
        return {};
    }

    auto candidate = resolveHotkeyBinding(config);
    if (!candidate) {
        return std::unexpected(candidate.error());
    }
    if (registeredId_ != 0 && owner_ == owner && binding_ == *candidate) {
        return {};
    }

    const int candidateId = registeredId_ == primaryId ? replacementId : primaryId;
    if (!RegisterHotKey(
            owner,
            candidateId,
            candidate->modifiers | MOD_NOREPEAT,
            candidate->virtualKey)) {
        return std::unexpected(HotkeyError{
            HotkeyErrorCode::RegistrationFailed,
            GetLastError(),
        });
    }

    if (registeredId_ != 0 && !UnregisterHotKey(owner_, registeredId_)) {
        const auto systemCode = GetLastError();
        UnregisterHotKey(owner, candidateId);
        return std::unexpected(HotkeyError{
            HotkeyErrorCode::UnregistrationFailed,
            systemCode,
        });
    }

    owner_ = owner;
    registeredId_ = candidateId;
    binding_ = *candidate;
    return {};
}

void GlobalHotkey::unregister() noexcept
{
    if (registeredId_ != 0) {
        UnregisterHotKey(owner_, registeredId_);
    }
    owner_ = nullptr;
    registeredId_ = 0;
    binding_ = {};
}

bool GlobalHotkey::isRegistered() const noexcept
{
    return registeredId_ != 0;
}

bool GlobalHotkey::handlesMessage(const WPARAM hotkeyId) const noexcept
{
    return registeredId_ != 0 && hotkeyId == static_cast<WPARAM>(registeredId_);
}

const HotkeyBinding* GlobalHotkey::binding() const noexcept
{
    return isRegistered() ? &binding_ : nullptr;
}

} // namespace hlaunch::platform::windows
