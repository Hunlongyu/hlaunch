#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/startup_registration.h"

#include <doctest/doctest.h>
#include <wil/resource.h>

#include <Windows.h>

#include <string>
#include <vector>

namespace {

std::wstring readRegistryString(
    const hlaunch::platform::windows::StartupRegistrationLocation& location)
{
    wil::unique_hkey key{};
    REQUIRE(RegOpenKeyExW(
        location.root,
        location.subkey.c_str(),
        0,
        KEY_QUERY_VALUE,
        key.put()) == ERROR_SUCCESS);
    DWORD type{};
    DWORD size{};
    REQUIRE(RegQueryValueExW(
        key.get(), location.valueName.c_str(), nullptr, &type, nullptr, &size)
        == ERROR_SUCCESS);
    REQUIRE(type == REG_SZ);
    std::vector<wchar_t> buffer(size / sizeof(wchar_t));
    REQUIRE(RegQueryValueExW(
        key.get(),
        location.valueName.c_str(),
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(buffer.data()),
        &size) == ERROR_SUCCESS);
    return buffer.data();
}

} // namespace

TEST_CASE("PLAT-AUTOSTART-001 command quotes executable and preserves forced portable mode")
{
    using hlaunch::platform::windows::buildStartupCommand;
    const std::filesystem::path executable{L"C:\\Program Files\\HLaunch\\HLaunch.exe"};
    CHECK(buildStartupCommand(executable, false)
        == L"\"C:\\Program Files\\HLaunch\\HLaunch.exe\"");
    CHECK(buildStartupCommand(executable, true)
        == L"\"C:\\Program Files\\HLaunch\\HLaunch.exe\" --portable");
    CHECK(buildStartupCommand({}, false).empty());
}

TEST_CASE("PLAT-AUTOSTART-001 enables queries and removes only the configured value")
{
    using namespace hlaunch::platform::windows;
    const auto suffix = std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(GetTickCount64());
    const StartupRegistrationLocation location{
        .root = HKEY_CURRENT_USER,
        .subkey = L"Software\\HLaunch.Tests.StartupRegistration-" + suffix,
        .valueName = L"HLaunch-Test",
    };
    const auto cleanup = wil::scope_exit([&location] {
        RegDeleteTreeW(location.root, location.subkey.c_str());
    });

    const auto initial = isStartupEnabled(location);
    REQUIRE(initial.has_value());
    CHECK_FALSE(*initial);

    const std::filesystem::path executable{L"C:\\Portable Tools\\HLaunch.exe"};
    REQUIRE(setStartupEnabled(executable, true, true, location).has_value());
    const auto enabled = isStartupEnabled(location);
    REQUIRE(enabled.has_value());
    CHECK(*enabled);
    CHECK(readRegistryString(location)
        == L"\"C:\\Portable Tools\\HLaunch.exe\" --portable");

    wil::unique_hkey key{};
    REQUIRE(RegOpenKeyExW(
        location.root,
        location.subkey.c_str(),
        0,
        KEY_SET_VALUE | KEY_QUERY_VALUE,
        key.put()) == ERROR_SUCCESS);
    constexpr wchar_t unrelatedValue[] = L"leave-me";
    REQUIRE(RegSetValueExW(
        key.get(),
        L"Unrelated",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(unrelatedValue),
        sizeof(unrelatedValue)) == ERROR_SUCCESS);

    REQUIRE(setStartupEnabled(executable, true, false, location).has_value());
    const auto disabled = isStartupEnabled(location);
    REQUIRE(disabled.has_value());
    CHECK_FALSE(*disabled);
    wchar_t unrelatedBuffer[32]{};
    DWORD unrelatedSize = sizeof(unrelatedBuffer);
    CHECK(RegGetValueW(
        key.get(),
        nullptr,
        L"Unrelated",
        RRF_RT_REG_SZ,
        nullptr,
        unrelatedBuffer,
        &unrelatedSize) == ERROR_SUCCESS);
    CHECK(std::wstring_view{unrelatedBuffer} == L"leave-me");
}
