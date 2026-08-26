#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/global_hotkey.h"

#include <doctest/doctest.h>

#include <Windows.h>

#include <array>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

hlaunch::core::HotkeyConfig makeHotkey(
    std::string key,
    std::vector<hlaunch::core::HotkeyModifier> modifiers)
{
    return hlaunch::core::HotkeyConfig{
        .enabled = true,
        .modifiers = std::move(modifiers),
        .key = std::move(key),
        .behavior = hlaunch::core::HotkeyBehavior::Toggle,
    };
}

class TestWindow final {
public:
    TestWindow()
    {
        constexpr wchar_t className[] = L"HLaunch.GlobalHotkeyTestWindow.v1";
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(WNDCLASSEXW);
        windowClass.lpfnWndProc = &TestWindow::windowProcedure;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = className;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        window_ = CreateWindowExW(
            0,
            className,
            L"",
            WS_OVERLAPPED,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            instance,
            this);
    }

    ~TestWindow()
    {
        if (window_) {
            DestroyWindow(window_);
        }
    }

    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;

    [[nodiscard]] HWND get() const noexcept
    {
        return window_;
    }

    [[nodiscard]] WPARAM waitForHotkey(
        const std::array<WORD, 3>& keys,
        const std::chrono::milliseconds timeout)
    {
        lastHotkeyId_ = 0;
        std::array<INPUT, 6> inputs{};
        for (std::size_t index = 0; index < keys.size(); ++index) {
            inputs[index].type = INPUT_KEYBOARD;
            inputs[index].ki.wVk = keys[index];
            inputs[inputs.size() - 1 - index].type = INPUT_KEYBOARD;
            inputs[inputs.size() - 1 - index].ki.wVk = keys[index];
            inputs[inputs.size() - 1 - index].ki.dwFlags = KEYEVENTF_KEYUP;
        }
        if (SendInput(
                static_cast<UINT>(inputs.size()),
                inputs.data(),
                sizeof(INPUT)) != inputs.size()) {
            return 0;
        }

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (lastHotkeyId_ == 0 && std::chrono::steady_clock::now() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return lastHotkeyId_;
    }

private:
    static LRESULT CALLBACK windowProcedure(
        const HWND window,
        const UINT message,
        const WPARAM wParam,
        const LPARAM lParam)
    {
        TestWindow* self = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam); // NOLINT(performance-no-int-to-ptr): Win32 LPARAM carries CREATESTRUCTW.
            self = static_cast<TestWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else {
            self = reinterpret_cast<TestWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr): Win32 stores this pointer as LONG_PTR.
        }
        if (self && message == WM_HOTKEY) {
            self->lastHotkeyId_ = wParam;
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    HWND window_{};
    WPARAM lastHotkeyId_{};
};

} // namespace

TEST_CASE("ACT-HOTKEY-001 resolves the default structured Alt+Space binding")
{
    const auto binding = hlaunch::platform::windows::resolveHotkeyBinding({});

    REQUIRE(binding.has_value());
    CHECK(binding->modifiers == MOD_ALT);
    CHECK(binding->virtualKey == VK_SPACE);
}

TEST_CASE("ACT-HOTKEY-001 accepts stable named, alphanumeric and function keys")
{
    using hlaunch::core::HotkeyModifier;
    const auto letter = hlaunch::platform::windows::resolveHotkeyBinding(
        makeHotkey("K", {HotkeyModifier::Control, HotkeyModifier::Shift}));
    const auto function = hlaunch::platform::windows::resolveHotkeyBinding(
        makeHotkey("F24", {HotkeyModifier::Alt}));
    const auto navigation = hlaunch::platform::windows::resolveHotkeyBinding(
        makeHotkey("PageDown", {HotkeyModifier::Win}));

    REQUIRE(letter.has_value());
    REQUIRE(function.has_value());
    REQUIRE(navigation.has_value());
    CHECK(letter->virtualKey == 'K');
    CHECK(function->virtualKey == VK_F24);
    CHECK(navigation->virtualKey == VK_NEXT);
}

TEST_CASE("ACT-HOTKEY-001 rejects ambiguous or reserved configurations")
{
    using hlaunch::core::HotkeyModifier;
    CHECK_FALSE(hlaunch::platform::windows::resolveHotkeyBinding(
        makeHotkey("space", {HotkeyModifier::Alt})).has_value());
    CHECK_FALSE(hlaunch::platform::windows::resolveHotkeyBinding(
        makeHotkey("F12", {HotkeyModifier::Alt})).has_value());
    CHECK_FALSE(hlaunch::platform::windows::resolveHotkeyBinding(
        makeHotkey("Space", {HotkeyModifier::Alt, HotkeyModifier::Alt})).has_value());
}

TEST_CASE("ACT-HOTKEY-001 conflict keeps the previous registration and permits retry")
{
    using hlaunch::core::HotkeyModifier;
    TestWindow activeWindow{};
    TestWindow blockingWindow{};
    REQUIRE(activeWindow.get() != nullptr);
    REQUIRE(blockingWindow.get() != nullptr);

    const auto original = makeHotkey(
        "F24",
        {HotkeyModifier::Control, HotkeyModifier::Shift});
    const auto conflicting = makeHotkey(
        "F23",
        {HotkeyModifier::Alt, HotkeyModifier::Shift});

    hlaunch::platform::windows::GlobalHotkey active{};
    hlaunch::platform::windows::GlobalHotkey blocker{};
    REQUIRE(active.apply(activeWindow.get(), original).has_value());
    const auto deliveredId = activeWindow.waitForHotkey(
        {VK_CONTROL, VK_SHIFT, VK_F24},
        std::chrono::milliseconds{500});
    CHECK(deliveredId != 0);
    CHECK(active.handlesMessage(deliveredId));
    REQUIRE(blocker.apply(blockingWindow.get(), conflicting).has_value());
    const auto originalBinding = *active.binding();

    const auto failedRebind = active.apply(activeWindow.get(), conflicting);
    REQUIRE_FALSE(failedRebind.has_value());
    CHECK(failedRebind.error().code
        == hlaunch::platform::windows::HotkeyErrorCode::RegistrationFailed);
    REQUIRE(active.binding() != nullptr);
    CHECK(*active.binding() == originalBinding);

    blocker.unregister();
    CHECK(active.apply(activeWindow.get(), conflicting).has_value());
    REQUIRE(active.binding() != nullptr);
    CHECK(active.binding()->virtualKey == VK_F23);

    auto disabled = conflicting;
    disabled.enabled = false;
    CHECK(active.apply(activeWindow.get(), disabled).has_value());
    CHECK_FALSE(active.isRegistered());
}
