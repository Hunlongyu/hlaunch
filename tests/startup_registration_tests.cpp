#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "platform/windows/startup_registration.h"

#include <doctest/doctest.h>
#include <taskschd.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <filesystem>
#include <string>

namespace {

using namespace hlaunch::platform::windows;

wil::unique_bstr bstr(const std::wstring& text)
{
    return wil::unique_bstr{SysAllocString(text.c_str())};
}

struct Fixture {
    const std::wstring suffix = std::to_wstring(GetCurrentProcessId()) + L"-"
        + std::to_wstring(GetTickCount64());
    StartupRegistrationLocation location{
        .taskName = L"HLaunch.Tests.Logon-" + suffix,
        .root = HKEY_CURRENT_USER,
        .subkey = L"Software\\HLaunch.Tests.StartupRegistration-" + suffix,
        .valueName = L"HLaunch-Test",
        .approvalSubkey = L"Software\\HLaunch.Tests.StartupRegistration-" + suffix + L"\\Approval",
    };
    const std::filesystem::path directory = std::filesystem::temp_directory_path()
        / (L"HLaunch Startup Tests-" + suffix);
    const std::filesystem::path executable = directory / L"启动 & Launcher.exe";
    winrt::com_ptr<ITaskService> service;
    winrt::com_ptr<ITaskFolder> folder;

    Fixture()
    {
        REQUIRE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
        REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, nullptr,
            CLSCTX_INPROC_SERVER, __uuidof(ITaskService), service.put_void())));
        const VARIANT empty{};
        REQUIRE(SUCCEEDED(service->Connect(empty, empty, empty, empty)));
        REQUIRE(SUCCEEDED(service->GetFolder(bstr(L"\\").get(), folder.put())));
        std::filesystem::create_directory(directory);
        wchar_t ownPath[32'768]{};
        REQUIRE(GetModuleFileNameW(nullptr, ownPath, 32'768) != 0);
        REQUIRE(CopyFileW(ownPath, executable.c_str(), TRUE));
    }

    ~Fixture()
    {
        if (folder) {
            folder->DeleteTask(bstr(location.taskName).get(), 0);
        }
        RegDeleteTreeW(location.root, location.subkey.c_str());
        std::error_code ignored;
        std::filesystem::remove(executable, ignored);
        std::filesystem::remove(directory, ignored);
        folder = nullptr;
        service = nullptr;
        CoUninitialize();
    }

    winrt::com_ptr<IRegisteredTask> task()
    {
        winrt::com_ptr<IRegisteredTask> result;
        REQUIRE(SUCCEEDED(folder->GetTask(bstr(location.taskName).get(), result.put())));
        return result;
    }

    void writeLegacy(const std::wstring& command, const wchar_t* name = L"HLaunch-Test")
    {
        wil::unique_hkey key;
        REQUIRE(RegCreateKeyExW(location.root, location.subkey.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, key.put(), nullptr) == ERROR_SUCCESS);
        REQUIRE(RegSetValueExW(key.get(), name, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    }

    bool hasLegacy(const wchar_t* name = L"HLaunch-Test")
    {
        DWORD bytes{};
        return RegGetValueW(location.root, location.subkey.c_str(), name,
            RRF_RT_REG_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS;
    }

    bool enabled()
    {
        const auto state = isStartupEnabled(executable, location);
        REQUIRE_MESSAGE(state.has_value(), "startup query failed");
        return *state;
    }

    std::wstring xml()
    {
        wil::unique_bstr result;
        REQUIRE(SUCCEEDED(task()->get_Xml(result.put())));
        return result.get();
    }
};

} // namespace

TEST_CASE("PLAT-AUTOSTART-001 task uses current interactive user without elevation or runtime limit")
{
    Fixture fixture;
    CHECK_FALSE(fixture.enabled());
    REQUIRE(setStartupEnabled(fixture.executable, true, true, fixture.location).has_value());
    CHECK(fixture.enabled());
    const auto xml = fixture.xml();
    CHECK(xml.find(L"<LogonType>InteractiveToken</LogonType>") != std::wstring::npos);
    winrt::com_ptr<ITaskDefinition> definition;
    REQUIRE(SUCCEEDED(fixture.task()->get_Definition(definition.put())));
    winrt::com_ptr<IPrincipal> principal;
    REQUIRE(SUCCEEDED(definition->get_Principal(principal.put())));
    TASK_RUNLEVEL_TYPE runLevel{};
    REQUIRE(SUCCEEDED(principal->get_RunLevel(&runLevel)));
    CHECK(runLevel == TASK_RUNLEVEL_LUA);
    winrt::com_ptr<ITaskSettings> settings;
    REQUIRE(SUCCEEDED(definition->get_Settings(settings.put())));
    int priority{};
    REQUIRE(SUCCEEDED(settings->get_Priority(&priority)));
    CHECK(priority == 6);
    winrt::com_ptr<ITriggerCollection> triggers;
    REQUIRE(SUCCEEDED(definition->get_Triggers(triggers.put())));
    winrt::com_ptr<ITrigger> trigger;
    REQUIRE(SUCCEEDED(triggers->get_Item(1, trigger.put())));
    const auto logon = trigger.as<ILogonTrigger>();
    wil::unique_bstr delay;
    REQUIRE(SUCCEEDED(logon->get_Delay(delay.put())));
    CHECK((!delay || std::wstring_view{delay.get()}.empty()
        || std::wstring_view{delay.get()} == L"PT0S"));
    CHECK(xml.find(L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>") != std::wstring::npos);
    CHECK(xml.find(L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>") != std::wstring::npos);
    CHECK(xml.find(L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>") != std::wstring::npos);
    CHECK(xml.find(L"<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>") != std::wstring::npos);
    CHECK(xml.find(L"<Arguments>--autostart --portable</Arguments>") != std::wstring::npos);
    CHECK(xml.find(L"启动 &amp; Launcher.exe") != std::wstring::npos);
    CHECK(xml.find(L"<WorkingDirectory>" + fixture.directory.native()) != std::wstring::npos);

    REQUIRE(setStartupEnabled(fixture.executable, false, true, fixture.location).has_value());
    CHECK(fixture.xml().find(L"<Arguments>--autostart</Arguments>") != std::wstring::npos);
    CHECK_FALSE(*isStartupEnabled(fixture.directory / L"Moved.exe", fixture.location));
    REQUIRE(fixture.task()->put_Enabled(VARIANT_FALSE) == S_OK);
    CHECK_FALSE(fixture.enabled());
    REQUIRE(setStartupEnabled(fixture.executable, false, true, fixture.location).has_value());
    CHECK(fixture.enabled());
    REQUIRE(setStartupEnabled(fixture.executable, false, false, fixture.location).has_value());
    CHECK_FALSE(fixture.enabled());
    REQUIRE(setStartupEnabled(fixture.executable, false, false, fixture.location).has_value());
}

TEST_CASE("PLAT-AUTOSTART-001 migrates only this executable and preserves legacy portable mode")
{
    Fixture fixture;
    fixture.writeLegacy(L"leave-me", L"Unrelated");
    REQUIRE(migrateStartupRegistration(fixture.executable, false, fixture.location).has_value());
    CHECK_FALSE(fixture.enabled());
    fixture.writeLegacy(buildStartupCommand(fixture.directory / L"Other.exe", false));
    REQUIRE(migrateStartupRegistration(fixture.executable, false, fixture.location).has_value());
    CHECK_FALSE(fixture.enabled());
    CHECK(fixture.hasLegacy());
    fixture.writeLegacy(buildStartupCommand(fixture.executable, true));
    REQUIRE(migrateStartupRegistration(fixture.executable, false, fixture.location).has_value());
    CHECK(fixture.enabled());
    CHECK_FALSE(fixture.hasLegacy());
    CHECK(fixture.hasLegacy(L"Unrelated"));
    CHECK(fixture.xml().find(L"--autostart --portable") != std::wstring::npos);
    REQUIRE(migrateStartupRegistration(fixture.executable, false, fixture.location).has_value());
    CHECK(fixture.enabled());
    REQUIRE(setStartupEnabled(fixture.executable, false, false, fixture.location).has_value());
    CHECK(fixture.hasLegacy(L"Unrelated"));
}

TEST_CASE("PLAT-AUTOSTART-001 failed task creation keeps legacy registration")
{
    Fixture fixture;
    fixture.writeLegacy(buildStartupCommand(fixture.executable, false));
    auto invalidLocation = fixture.location;
    invalidLocation.taskName = L"HLaunch.Tests.Invalid?Name-" + fixture.suffix;
    CHECK_FALSE(migrateStartupRegistration(fixture.executable, false, invalidLocation).has_value());
    CHECK(fixture.hasLegacy());
    CHECK_FALSE(fixture.enabled());
    CHECK_FALSE(setStartupEnabled({}, false, true, fixture.location).has_value());
    CHECK_FALSE(setStartupEnabled(L"relative.exe", false, true, fixture.location).has_value());
    CHECK_FALSE(setStartupEnabled(fixture.directory, false, true, fixture.location).has_value());
    CHECK(fixture.hasLegacy());
}

TEST_CASE("PLAT-AUTOSTART-001 migration does not re-enable an externally disabled task")
{
    Fixture fixture;
    REQUIRE(setStartupEnabled(fixture.executable, false, true, fixture.location).has_value());
    REQUIRE(fixture.task()->put_Enabled(VARIANT_FALSE) == S_OK);
    fixture.writeLegacy(buildStartupCommand(fixture.executable, false));
    REQUIRE(migrateStartupRegistration(fixture.executable, false, fixture.location).has_value());
    CHECK_FALSE(fixture.enabled());
    CHECK_FALSE(fixture.hasLegacy());
}

TEST_CASE("PLAT-AUTOSTART-001 refuses to overwrite or delete an unrelated task")
{
    Fixture fixture;
    REQUIRE(setStartupEnabled(fixture.executable, false, true, fixture.location).has_value());
    winrt::com_ptr<ITaskDefinition> definition;
    REQUIRE(SUCCEEDED(fixture.task()->get_Definition(definition.put())));
    winrt::com_ptr<IRegistrationInfo> info;
    REQUIRE(SUCCEEDED(definition->get_RegistrationInfo(info.put())));
    REQUIRE(SUCCEEDED(info->put_Source(bstr(L"Other application").get())));
    const VARIANT empty{};
    winrt::com_ptr<IRegisteredTask> updated;
    REQUIRE(SUCCEEDED(fixture.folder->RegisterTaskDefinition(bstr(fixture.location.taskName).get(),
        definition.get(), TASK_UPDATE, empty, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, updated.put())));
    fixture.writeLegacy(buildStartupCommand(fixture.executable, false));
    CHECK_FALSE(isStartupEnabled(fixture.executable, fixture.location).has_value());
    CHECK_FALSE(setStartupEnabled(fixture.executable, false, true, fixture.location).has_value());
    CHECK_FALSE(setStartupEnabled(fixture.executable, false, false, fixture.location).has_value());
    CHECK_FALSE(migrateStartupRegistration(fixture.executable, false, fixture.location).has_value());
    CHECK(fixture.hasLegacy());
    CHECK(fixture.xml().find(L"Other application") != std::wstring::npos);
}

TEST_CASE("PLAT-AUTOSTART-001 migration respects disabled or unknown legacy approval")
{
    Fixture fixture;
    fixture.writeLegacy(buildStartupCommand(fixture.executable, false));
    wil::unique_hkey key;
    REQUIRE(RegCreateKeyExW(fixture.location.root, fixture.location.approvalSubkey.c_str(),
        0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, key.put(), nullptr) == ERROR_SUCCESS);
    for (const DWORD state : {3UL, 7UL, 42UL}) {
        const DWORD data[3]{state, 0, 0};
        REQUIRE(RegSetValueExW(key.get(), fixture.location.valueName.c_str(), 0, REG_BINARY,
            reinterpret_cast<const BYTE*>(data), sizeof(data)) == ERROR_SUCCESS);
        REQUIRE(migrateStartupRegistration(fixture.executable, false, fixture.location).has_value());
        CHECK_FALSE(fixture.enabled());
        CHECK(fixture.hasLegacy());
    }
    const DWORD enabled[3]{2, 0, 0};
    REQUIRE(RegSetValueExW(key.get(), fixture.location.valueName.c_str(), 0, REG_BINARY,
        reinterpret_cast<const BYTE*>(enabled), sizeof(enabled)) == ERROR_SUCCESS);
    REQUIRE(migrateStartupRegistration(fixture.executable, false, fixture.location).has_value());
    CHECK(fixture.enabled());
    CHECK_FALSE(fixture.hasLegacy());
}
