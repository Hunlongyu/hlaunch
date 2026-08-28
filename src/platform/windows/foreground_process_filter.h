#pragma once

#include "core/data_model.h"

#include <Windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlaunch::platform::windows {

class ForegroundProcessFilter final {
public:
    ForegroundProcessFilter() = default;
    explicit ForegroundProcessFilter(const core::ScreenEdgeConfig& config);

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool suppresses(std::optional<std::wstring_view> executableName) const noexcept;

private:
    std::vector<std::wstring> blocklist_{};
    std::vector<std::wstring> allowlist_{};
};

[[nodiscard]] std::optional<std::wstring> executableNameForWindow(HWND window) noexcept;

} // namespace hlaunch::platform::windows
