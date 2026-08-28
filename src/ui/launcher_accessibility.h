#pragma once

#include <Windows.h>
#include <Unknwn.h>
#include <UIAutomation.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hlaunch::ui {

inline constexpr UINT launcherAccessibilityInvokeMessage = WM_APP + 0x47U;
inline constexpr UINT launcherAccessibilityFocusMessage = WM_APP + 0x48U;

struct LauncherAccessibleNode final {
    std::wstring key;
    std::wstring parentKey;
    std::wstring name;
    CONTROLTYPEID controlType{};
    UiaRect bounds{};
    bool keyboardFocusable{};
    bool hasKeyboardFocus{};
    bool enabled{true};
    bool offscreen{};
    bool invokable{};
    bool selectionContainer{};
    bool selectable{};
    bool selected{};
    int positionInSet{};
    int sizeOfSet{};
};

struct LauncherAccessibilityCallbacks final {
    HWND window{};
    std::function<std::vector<LauncherAccessibleNode>()> snapshot;
    std::function<void(std::wstring_view)> invoke;
    std::function<void(std::wstring_view)> focus;
};

class LauncherAccessibility final {
public:
    explicit LauncherAccessibility(LauncherAccessibilityCallbacks callbacks);
    ~LauncherAccessibility();

    LauncherAccessibility(const LauncherAccessibility&) = delete;
    LauncherAccessibility& operator=(const LauncherAccessibility&) = delete;

    [[nodiscard]] LRESULT handleGetObject(WPARAM wParam, LPARAM lParam) noexcept;
    void raiseStructureChanged() noexcept;
    void raiseFocusChanged(std::wstring_view key) noexcept;
    void raiseSelectionChanged(std::wstring_view key) noexcept;
    void refresh() noexcept;
    void disconnect() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hlaunch::ui
