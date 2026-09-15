#include "platform/windows/startup_registration.h"

#include <Sddl.h>
#include <taskschd.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <optional>
#include <vector>

namespace hlaunch::platform::windows {
namespace {

constexpr wchar_t taskSource[] = L"Hunlongyu.HLaunch";

wil::unique_bstr bstr(const std::wstring_view text)
{
    wil::unique_bstr value{SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))};
    if (!value) {
        throw winrt::hresult_error{E_OUTOFMEMORY};
    }
    return value;
}

bool equalText(const std::wstring_view left, const std::wstring_view right)
{
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                                right.data(), static_cast<int>(right.size()), TRUE)
        == CSTR_EQUAL;
}

bool missing(const HRESULT result)
{
    return result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
        || result == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

bool sameUser(const std::wstring_view account, const std::wstring& expectedSid)
{
    if (equalText(account, expectedSid)) {
        return true;
    }
    // Task Scheduler can normalize SID inputs to DOMAIN\user on COM readback.
    DWORD sidBytes{}, domainChars{};
    SID_NAME_USE use{};
    const std::wstring name{account};
    LookupAccountNameW(nullptr, name.c_str(), nullptr, &sidBytes, nullptr, &domainChars, &use);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sidBytes == 0) {
        return false;
    }
    std::vector<BYTE> sid(sidBytes);
    std::wstring domain(domainChars, L'\0');
    if (!LookupAccountNameW(nullptr, name.c_str(), sid.data(), &sidBytes,
                           domain.data(), &domainChars, &use)) {
        return false;
    }
    wil::unique_hlocal_string actual;
    winrt::check_bool(ConvertSidToStringSidW(sid.data(), actual.put()));
    return equalText(actual.get(), expectedSid);
}

std::wstring currentUserSid()
{
    wil::unique_handle token;
    winrt::check_bool(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()));
    DWORD bytes{};
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    winrt::check_bool(GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes != 0);
    std::vector<BYTE> buffer(bytes);
    winrt::check_bool(GetTokenInformation(token.get(), TokenUser, buffer.data(), bytes, &bytes));
    wil::unique_hlocal_string sid;
    winrt::check_bool(ConvertSidToStringSidW(
        reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, sid.put()));
    return sid.get();
}

struct TaskConnection {
    std::wstring sid{currentUserSid()};
    std::wstring name;
    winrt::com_ptr<ITaskService> service;
    winrt::com_ptr<ITaskFolder> folder;

    explicit TaskConnection(const StartupRegistrationLocation& location)
        : name(location.taskName.empty() ? L"HLaunch.Logon." + sid : location.taskName)
    {
        winrt::check_hresult(CoCreateInstance(CLSID_TaskScheduler, nullptr,
            CLSCTX_INPROC_SERVER, __uuidof(ITaskService), service.put_void()));
        const VARIANT empty{};
        winrt::check_hresult(service->Connect(empty, empty, empty, empty));
        winrt::check_hresult(service->GetFolder(bstr(L"\\").get(), folder.put()));
    }

    winrt::com_ptr<IRegisteredTask> find() const
    {
        winrt::com_ptr<IRegisteredTask> task;
        const auto result = folder->GetTask(bstr(name).get(), task.put());
        if (missing(result)) {
            return {};
        }
        winrt::check_hresult(result);
        return task;
    }
};

winrt::com_ptr<ITaskDefinition> ownedDefinition(IRegisteredTask* task)
{
    winrt::com_ptr<ITaskDefinition> definition;
    winrt::check_hresult(task->get_Definition(definition.put()));
    winrt::com_ptr<IRegistrationInfo> info;
    winrt::check_hresult(definition->get_RegistrationInfo(info.put()));
    wil::unique_bstr source;
    winrt::check_hresult(info->get_Source(source.put()));
    // A name collision must not overwrite an unrelated task.
    if (!source || std::wstring_view{source.get()} != taskSource) {
        throw winrt::hresult_error{E_ACCESSDENIED};
    }
    return definition;
}

std::optional<std::wstring> legacyCommand(const StartupRegistrationLocation& location)
{
    wil::unique_hkey key;
    auto result = RegOpenKeyExW(location.root, location.subkey.c_str(), 0,
                                KEY_QUERY_VALUE, key.put());
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
        return std::nullopt;
    }
    winrt::check_win32(result);
    DWORD bytes{};
    result = RegGetValueW(key.get(), nullptr, location.valueName.c_str(),
                         RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND) {
        return std::nullopt;
    }
    winrt::check_win32(result);
    if (bytes == 0 || bytes > 65'536 || bytes % sizeof(wchar_t) != 0) {
        throw winrt::hresult_error{HRESULT_FROM_WIN32(ERROR_INVALID_DATA)};
    }
    std::wstring command(bytes / sizeof(wchar_t), L'\0');
    winrt::check_win32(RegGetValueW(key.get(), nullptr, location.valueName.c_str(),
        RRF_RT_REG_SZ, nullptr, command.data(), &bytes));
    command.resize(wcsnlen(command.data(), command.size()));
    return command;
}

void removeLegacyValue(const StartupRegistrationLocation& location)
{
    wil::unique_hkey key;
    const auto result = RegOpenKeyExW(location.root, location.subkey.c_str(), 0,
                                     KEY_SET_VALUE, key.put());
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
        return;
    }
    winrt::check_win32(result);
    const auto deleted = RegDeleteValueW(key.get(), location.valueName.c_str());
    if (deleted != ERROR_FILE_NOT_FOUND) {
        winrt::check_win32(deleted);
    }
}

bool legacyApproved(const StartupRegistrationLocation& location)
{
    DWORD bytes{};
    const auto result = RegGetValueW(location.root, location.approvalSubkey.c_str(),
        location.valueName.c_str(), RRF_RT_REG_BINARY, nullptr, nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    // This Explorer record is not a public API. Migrate only the known enabled
    // record (state 2); leave disabled, malformed and future formats untouched.
    if (result == ERROR_UNSUPPORTED_TYPE) {
        return false;
    }
    winrt::check_win32(result);
    if (bytes != 12) {
        return false;
    }
    DWORD data[3]{};
    DWORD actualBytes = sizeof(data);
    winrt::check_win32(RegGetValueW(location.root, location.approvalSubkey.c_str(),
        location.valueName.c_str(), RRF_RT_REG_BINARY, nullptr, data, &actualBytes));
    return actualBytes == sizeof(data) && data[0] == 2;
}

void registerTask(const TaskConnection& connection,
                  const std::filesystem::path& executablePath, const bool forcePortable)
{
    if (!executablePath.is_absolute() || executablePath.native().find(L'"') != std::wstring::npos
        || executablePath.native().find(L'\0') != std::wstring::npos) {
        throw winrt::hresult_error{E_INVALIDARG};
    }
    const auto attributes = GetFileAttributesW(executablePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        winrt::throw_last_error();
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        throw winrt::hresult_error{E_INVALIDARG};
    }
    if (const auto existing = connection.find()) {
        static_cast<void>(ownedDefinition(existing.get()));
    }

    winrt::com_ptr<ITaskDefinition> definition;
    winrt::check_hresult(connection.service->NewTask(0, definition.put()));
    winrt::com_ptr<IRegistrationInfo> info;
    winrt::check_hresult(definition->get_RegistrationInfo(info.put()));
    winrt::check_hresult(info->put_Source(bstr(taskSource).get()));
    winrt::check_hresult(info->put_Description(bstr(L"当前用户登录后启动 HLaunch").get()));

    winrt::com_ptr<IPrincipal> principal;
    winrt::check_hresult(definition->get_Principal(principal.put()));
    winrt::check_hresult(principal->put_UserId(bstr(connection.sid).get()));
    winrt::check_hresult(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN));
    winrt::check_hresult(principal->put_RunLevel(TASK_RUNLEVEL_LUA));

    winrt::com_ptr<ITaskSettings> settings;
    winrt::check_hresult(definition->get_Settings(settings.put()));
    winrt::check_hresult(settings->put_Enabled(VARIANT_TRUE));
    winrt::check_hresult(settings->put_StartWhenAvailable(VARIANT_TRUE));
    // Task Scheduler defaults to below-normal priority; this is an interactive launcher.
    winrt::check_hresult(settings->put_Priority(6));
    winrt::check_hresult(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE));
    winrt::check_hresult(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE));
    winrt::check_hresult(settings->put_ExecutionTimeLimit(bstr(L"PT0S").get()));
    winrt::check_hresult(settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW));

    winrt::com_ptr<ITriggerCollection> triggers;
    winrt::check_hresult(definition->get_Triggers(triggers.put()));
    winrt::com_ptr<ITrigger> trigger;
    winrt::check_hresult(triggers->Create(TASK_TRIGGER_LOGON, trigger.put()));
    const auto logon = trigger.as<ILogonTrigger>();
    winrt::check_hresult(logon->put_UserId(bstr(connection.sid).get()));
    // A launcher should be available as soon as Windows dispatches logon tasks.
    winrt::check_hresult(logon->put_Delay(bstr(L"PT0S").get()));
    winrt::check_hresult(logon->put_Enabled(VARIANT_TRUE));

    winrt::com_ptr<IActionCollection> actions;
    winrt::check_hresult(definition->get_Actions(actions.put()));
    winrt::com_ptr<IAction> action;
    winrt::check_hresult(actions->Create(TASK_ACTION_EXEC, action.put()));
    const auto exec = action.as<IExecAction>();
    winrt::check_hresult(exec->put_Path(bstr(executablePath.native()).get()));
    winrt::check_hresult(exec->put_Arguments(
        bstr(forcePortable ? L"--autostart --portable" : L"--autostart").get()));
    winrt::check_hresult(exec->put_WorkingDirectory(bstr(executablePath.parent_path().native()).get()));

    VARIANT user{};
    user.vt = VT_BSTR;
    auto sid = bstr(connection.sid);
    user.bstrVal = sid.get();
    const VARIANT empty{};
    winrt::com_ptr<IRegisteredTask> registered;
    winrt::check_hresult(connection.folder->RegisterTaskDefinition(bstr(connection.name).get(),
        definition.get(), TASK_CREATE_OR_UPDATE, user, empty, TASK_LOGON_INTERACTIVE_TOKEN,
        empty, registered.put()));
}

StartupRegistrationError currentError() noexcept
{
    return {static_cast<DWORD>(winrt::to_hresult())};
}

} // namespace

std::wstring buildStartupCommand(const std::filesystem::path& executablePath, const bool forcePortable)
{
    if (executablePath.empty()) {
        return {};
    }
    return L"\"" + executablePath.native() + L"\"" + (forcePortable ? L" --portable" : L"");
}

std::expected<bool, StartupRegistrationError> isStartupEnabled(
    const std::filesystem::path& executablePath, const StartupRegistrationLocation& location)
{
    try {
        const TaskConnection connection{location};
        const auto task = connection.find();
        if (!task) {
            return false;
        }
        const auto definition = ownedDefinition(task.get());
        VARIANT_BOOL enabled{};
        winrt::check_hresult(task->get_Enabled(&enabled));
        winrt::com_ptr<IPrincipal> principal;
        winrt::check_hresult(definition->get_Principal(principal.put()));
        TASK_LOGON_TYPE logonType{};
        TASK_RUNLEVEL_TYPE runLevel{};
        wil::unique_bstr user;
        winrt::check_hresult(principal->get_UserId(user.put()));
        winrt::check_hresult(principal->get_LogonType(&logonType));
        winrt::check_hresult(principal->get_RunLevel(&runLevel));
        if (enabled != VARIANT_TRUE || !user || !sameUser(user.get(), connection.sid)
            || logonType != TASK_LOGON_INTERACTIVE_TOKEN || runLevel != TASK_RUNLEVEL_LUA) {
            return false;
        }
        winrt::com_ptr<IActionCollection> actions;
        winrt::check_hresult(definition->get_Actions(actions.put()));
        LONG count{};
        winrt::check_hresult(actions->get_Count(&count));
        if (count != 1) {
            return false;
        }
        winrt::com_ptr<IAction> action;
        winrt::check_hresult(actions->get_Item(1, action.put()));
        const auto exec = action.try_as<IExecAction>();
        if (!exec) {
            return false;
        }
        wil::unique_bstr path, arguments;
        winrt::check_hresult(exec->get_Path(path.put()));
        winrt::check_hresult(exec->get_Arguments(arguments.put()));
        if (!path || !equalText(path.get(), executablePath.native()) || !arguments
            || (std::wstring_view{arguments.get()} != L"--autostart"
                && std::wstring_view{arguments.get()} != L"--autostart --portable")) {
            return false;
        }
        winrt::com_ptr<ITriggerCollection> triggers;
        winrt::check_hresult(definition->get_Triggers(triggers.put()));
        winrt::check_hresult(triggers->get_Count(&count));
        if (count != 1) {
            return false;
        }
        winrt::com_ptr<ITrigger> trigger;
        winrt::check_hresult(triggers->get_Item(1, trigger.put()));
        const auto logon = trigger.try_as<ILogonTrigger>();
        if (!logon) {
            return false;
        }
        wil::unique_bstr triggerUser;
        winrt::check_hresult(logon->get_UserId(triggerUser.put()));
        winrt::check_hresult(logon->get_Enabled(&enabled));
        const auto attributes = GetFileAttributesW(executablePath.c_str());
        return enabled == VARIANT_TRUE && triggerUser && sameUser(triggerUser.get(), connection.sid)
            && attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    } catch (...) {
        return std::unexpected(currentError());
    }
}

std::expected<void, StartupRegistrationError> setStartupEnabled(
    const std::filesystem::path& executablePath, const bool forcePortable,
    const bool enabled, const StartupRegistrationLocation& location)
{
    try {
        const TaskConnection connection{location};
        if (enabled) {
            registerTask(connection, executablePath, forcePortable);
        } else if (const auto task = connection.find()) {
            static_cast<void>(ownedDefinition(task.get()));
            winrt::check_hresult(connection.folder->DeleteTask(bstr(connection.name).get(), 0));
        }
        // Never remove the old registration before successful task creation.
        removeLegacyValue(location);
        return {};
    } catch (...) {
        return std::unexpected(currentError());
    }
}

std::expected<void, StartupRegistrationError> migrateStartupRegistration(
    const std::filesystem::path& executablePath, const bool forcePortable,
    const StartupRegistrationLocation& location)
{
    try {
        const auto legacy = legacyCommand(location);
        if (!legacy) {
            return {};
        }
        const bool legacyPortable = equalText(*legacy, buildStartupCommand(executablePath, true));
        if (!legacyPortable && !equalText(*legacy, buildStartupCommand(executablePath, false))) {
            return {};
        }
        const TaskConnection connection{location};
        if (const auto task = connection.find()) {
            static_cast<void>(ownedDefinition(task.get()));
            // Respect a task disabled by the user, including after a partial migration.
            removeLegacyValue(location);
            return {};
        }
        if (!legacyApproved(location)) {
            return {};
        }
        registerTask(connection, executablePath, forcePortable || legacyPortable);
        removeLegacyValue(location);
        return {};
    } catch (...) {
        return std::unexpected(currentError());
    }
}

} // namespace hlaunch::platform::windows
