#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hlaunch::core {

inline constexpr std::uint32_t currentSchemaVersion = 1;

enum class HotkeyModifier {
    Alt,
    Control,
    Shift,
    Win,
};

enum class HotkeyBehavior {
    Toggle,
};

enum class ScreenEdgeZone {
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

enum class ScreenEdgeMode {
    DesktopOuter,
    EveryMonitor,
};

struct HotkeyConfig {
    bool enabled{true};
    std::vector<HotkeyModifier> modifiers{HotkeyModifier::Alt};
    std::string key{"Space"};
    HotkeyBehavior behavior{HotkeyBehavior::Toggle};

    bool operator==(const HotkeyConfig&) const = default;
};

struct ScreenEdgeConfig {
    bool enabled{false};
    std::vector<ScreenEdgeZone> zones{ScreenEdgeZone::Left};
    ScreenEdgeMode edgeMode{ScreenEdgeMode::DesktopOuter};
    double thicknessDip{4.0};
    double cornerSizeDip{16.0};
    std::uint32_t dwellMs{300};
    std::uint32_t pollMs{40};
    std::uint32_t cooldownMs{500};
    bool disableOnFullscreen{true};

    bool operator==(const ScreenEdgeConfig&) const = default;
};

struct ActivationConfig {
    HotkeyConfig hotkey{};
    ScreenEdgeConfig screenEdge{};

    bool operator==(const ActivationConfig&) const = default;
};

struct ApplicationConfig {
    std::uint32_t schemaVersion{currentSchemaVersion};
    ActivationConfig activation{};

    bool operator==(const ApplicationConfig&) const = default;
};

enum class ItemType {
    Application,
    File,
    Folder,
    Url,
    Shortcut,
};

struct LaunchItem {
    std::string id{};
    ItemType type{ItemType::Application};
    std::string name{};
    std::string target{};
    std::vector<std::string> arguments{};
    std::optional<std::string> workingDirectory{};
    std::optional<std::string> icon{};
    bool runAsAdministrator{false};
    std::uint64_t launchCount{};
    std::optional<std::string> lastLaunchedAt{};

    bool operator==(const LaunchItem&) const = default;
};

struct Tab {
    std::string id{};
    std::string name{};
    std::vector<LaunchItem> items{};

    bool operator==(const Tab&) const = default;
};

struct ItemsDocument {
    std::uint32_t schemaVersion{currentSchemaVersion};
    std::vector<Tab> tabs{};

    bool operator==(const ItemsDocument&) const = default;
};

} // namespace hlaunch::core
