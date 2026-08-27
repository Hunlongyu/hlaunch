#include "app/application.h"

#include "core/data_validation.h"
#include "infrastructure/filesystem/data_paths.h"
#include "infrastructure/filesystem/data_store.h"
#include "infrastructure/logging/diagnostic_log.h"
#include "platform/windows/app_identity.h"
#include "platform/windows/shell_launcher.h"
#include "platform/windows/startup_registration.h"
#include "ui/system_appearance.h"
#include "ui/task_dialog.h"

#include <Ole2.h>
#include <wil/resource.h>

#include <array>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace hlaunch::app {
namespace {

constexpr UINT itemsSaveFailedMessage = WM_APP + 0x41U;

std::filesystem::path executablePath()
{
    std::wstring buffer(32'768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return buffer;
}

void showStartupError(const wchar_t* message)
{
    ui::showTaskMessage(nullptr, L"HLaunch", message, ui::TaskDialogIcon::Error);
}

void showHotkeyError(const HWND owner, const platform::windows::HotkeyError& error)
{
    std::wstring message{};
    if (error.code == platform::windows::HotkeyErrorCode::InvalidConfiguration) {
        message = L"全局快捷键配置无效，快捷键未启用。\n\n"
                  L"请修正 config.json 中的 activation.hotkey 后重新启动 HLaunch。";
    }
    else {
        message = L"无法注册配置的全局快捷键，快捷键未启用。\n\n"
                  L"该组合可能已被系统或其他程序占用。请关闭占用程序后重新启动 HLaunch。"
                  L"\n\n系统错误码：";
        message += std::to_wstring(error.systemCode);
    }
    ui::showTaskMessage(
        owner, L"HLaunch 快捷键", L"全局快捷键不可用。",
        ui::TaskDialogIcon::Warning, message);
}

std::string_view loadSourceName(const infrastructure::filesystem::LoadSource source) noexcept
{
    using infrastructure::filesystem::LoadSource;
    switch (source) {
    case LoadSource::Defaults:
        return "defaults";
    case LoadSource::Primary:
        return "primary";
    case LoadSource::Backup:
        return "backup";
    }
    return "unknown";
}

std::size_t itemCount(const core::ItemsDocument& document) noexcept
{
    std::size_t count{};
    for (const auto& tab : document.tabs) {
        count += tab.items.size();
    }
    return count;
}

std::string_view activationCommandName(
    const platform::windows::ActivationCommand command) noexcept
{
    using platform::windows::ActivationCommand;
    switch (command) {
    case ActivationCommand::Show:
        return "show";
    case ActivationCommand::Hide:
        return "hide";
    case ActivationCommand::Toggle:
        return "toggle";
    }
    return "unknown";
}

platform::windows::WindowEffects windowEffectsFromAppearance(
    const core::AppearanceConfig& appearance) noexcept
{
    using core::BackdropMode;
    using platform::windows::WindowBackdrop;
    auto backdrop = WindowBackdrop::Acrylic;
    switch (appearance.backdrop) {
    case BackdropMode::Solid:
        backdrop = WindowBackdrop::Solid;
        break;
    case BackdropMode::Mica:
        backdrop = WindowBackdrop::Mica;
        break;
    case BackdropMode::Acrylic:
        backdrop = WindowBackdrop::Acrylic;
        break;
    case BackdropMode::Tabbed:
        backdrop = WindowBackdrop::Tabbed;
        break;
    }
    return {
        .backdrop = backdrop,
        .opacityPercent = appearance.opacityPercent,
    };
}

} // namespace

int Application::run(const HINSTANCE instance, const StartupOptions& options)
{
    instance_ = instance;
    executablePath_ = executablePath();
    forcePortable_ = options.portable;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto acquiredInstance = platform::windows::SingleInstance::acquire();
    if (!acquiredInstance) {
        showStartupError(L"无法建立单实例保护。请检查系统权限后重试。");
        return 1;
    }
    singleInstance_.emplace(std::move(*acquiredInstance));
    if (!singleInstance_->isPrimary()) {
        const auto command = options.activation.value_or(
            platform::windows::ActivationCommand::Show);
        return platform::windows::notifyPrimaryInstance(command) ? 0 : 2;
    }

    const auto oleResult = OleInitialize(nullptr);
    if (FAILED(oleResult)) {
        showStartupError(L"OLE 初始化失败，HLaunch 无法启动。");
        return 3;
    }
    const auto oleCleanup = wil::scope_exit([] { OleUninitialize(); });

    const auto paths = infrastructure::filesystem::resolveDataPaths({
        .executablePath = executablePath_,
        .forcePortable = options.portable,
    });
    if (!paths) {
        showStartupError(L"无法解析 HLaunch 数据目录。");
        return 4;
    }

    const auto logResult = infrastructure::logging::initialize({
        .directory = paths->logDirectory,
    });
    const auto logCleanup = wil::scope_exit([] {
        infrastructure::logging::write(infrastructure::logging::Level::Info, "application_stopped");
        infrastructure::logging::shutdown();
    });
    if (logResult) {
        infrastructure::logging::installUnhandledExceptionHandler();
        infrastructure::logging::write(
            infrastructure::logging::Level::Info,
            paths->portable ? "application_started mode=portable"
                            : "application_started mode=standard");
    }
    else {
        OutputDebugStringW(L"HLaunch could not initialize diagnostic logging.\n");
    }

    const auto identityResult = platform::windows::setProcessAppUserModelId();
    if (identityResult) {
        infrastructure::logging::write(
            infrastructure::logging::Level::Info,
            "app_user_model_id_set value=Hunlongyu.HLaunch");
    }
    else {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Warning,
            "app_user_model_id_set_failed",
            static_cast<unsigned long>(identityResult.error()));
    }

    const auto config = infrastructure::filesystem::loadConfig(paths->configFile);
    auto items = infrastructure::filesystem::loadItems(paths->itemsFile);
    if (!config || !config->value || !items || !items->value) {
        if (!config) {
            infrastructure::logging::writeSystemError(
                infrastructure::logging::Level::Error,
                "config_load_failed",
                config.error().systemCode);
        }
        else if (!config->value) {
            infrastructure::logging::write(
                infrastructure::logging::Level::Error,
                "config_load_rejected issue_count=" + std::to_string(config->issues.size()));
        }
        if (!items) {
            infrastructure::logging::writeSystemError(
                infrastructure::logging::Level::Error,
                "items_load_failed",
                items.error().systemCode);
        }
        else if (!items->value) {
            infrastructure::logging::write(
                infrastructure::logging::Level::Error,
                "items_load_rejected issue_count=" + std::to_string(items->issues.size()));
        }
        showStartupError(L"无法读取 HLaunch 数据文件。");
        return 5;
    }

    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "config_loaded source=" + std::string{loadSourceName(config->source)}
            + " issue_count=" + std::to_string(config->issues.size()));
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "items_loaded source=" + std::string{loadSourceName(items->source)}
            + " tab_count=" + std::to_string(items->value->tabs.size())
            + " item_count=" + std::to_string(itemCount(*items->value))
            + " issue_count=" + std::to_string(items->issues.size()));

    config_ = *config->value;
    configFile_ = paths->configFile;
    windowEffects_ = windowEffectsFromAppearance(config_.appearance);
    if (options.backdropSpecified) {
        windowEffects_.backdrop = options.windowEffects.backdrop;
    }
    if (options.opacitySpecified) {
        windowEffects_.opacityPercent = options.windowEffects.opacityPercent;
    }

    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "launcher_window_create_started");
    if (!launcher_.create(
            instance,
            windowEffects_,
            options.showSearch,
            std::move(*items->value),
            [this](const core::LaunchItem& item) { launch(item); },
            {},
            config_.appearance.theme)) {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Error,
            "launcher_window_create_failed",
            GetLastError());
        showStartupError(L"无法创建 HLaunch 窗口。");
        return 6;
    }
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "launcher_window_created");

    if (!createActivationWindow(instance)) {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Error,
            "activation_window_create_failed",
            GetLastError());
        showStartupError(L"无法创建 HLaunch 窗口。");
        return 6;
    }
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "activation_window_created");
    launcher_.setSettingsHandler([this] { showSettings(); });

    try {
        itemsSaver_.emplace(
            paths->itemsFile,
            [this](const infrastructure::filesystem::StoreError& error) {
                infrastructure::logging::writeSystemError(
                    infrastructure::logging::Level::Error,
                    "items_save_failed",
                    error.systemCode);
                if (activationWindow_) {
                    PostMessageW(activationWindow_, itemsSaveFailedMessage, 0, 0);
                }
            });
    }
    catch (const std::exception&) {
        infrastructure::logging::write(
            infrastructure::logging::Level::Error,
            "items_save_worker_create_failed");
        showStartupError(L"无法启动条目保存服务。");
        return 6;
    }
    const auto itemsSaverCleanup = wil::scope_exit([this] { itemsSaver_.reset(); });

    launcher_.setDocumentChangedHandler(
        [this](const core::ItemsDocument& document) {
            if (itemsSaver_) {
                itemsSaver_->submit(document);
            }
        });

    const auto& hotkeyConfig = config_.activation.hotkey;
    const auto hotkeyResult = hotkey_.apply(activationWindow_, hotkeyConfig);
    const bool hotkeyAvailable = hotkeyResult.has_value() && hotkey_.isRegistered();
    const bool screenEdgeStarted = screenEdge_.start(
        {
            .activationWindow = activationWindow_,
            .launcherWindow = launcher_.handle(),
        },
        config_.activation.screenEdge);
    const bool trayStarted = trayIcon_.start(
        activationWindow_,
        LoadIconW(nullptr, IDI_APPLICATION));

    if (hotkeyResult) {
        infrastructure::logging::write(
            infrastructure::logging::Level::Info,
            hotkeyAvailable ? "hotkey_ready" : "hotkey_disabled");
    }
    else {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Warning,
            "hotkey_registration_failed",
            hotkeyResult.error().systemCode);
    }
    infrastructure::logging::write(
        screenEdgeStarted ? infrastructure::logging::Level::Info
                          : infrastructure::logging::Level::Warning,
        screenEdgeStarted ? "screen_edge_service_ready" : "screen_edge_service_failed");
    infrastructure::logging::write(
        trayStarted ? infrastructure::logging::Level::Info
                    : infrastructure::logging::Level::Warning,
        trayStarted ? "tray_icon_ready" : "tray_icon_failed");

    if (options.activation) {
        execute(*options.activation);
    }
    else if (options.showSearch || !hotkeyAvailable) {
        // Keep the application reachable while tray and settings UI are still pending.
        execute(platform::windows::ActivationCommand::Show);
    }

    if (!hotkeyResult) {
        showHotkeyError(launcher_.handle(), hotkeyResult.error());
    }
    if (!screenEdgeStarted) {
        ui::showTaskMessage(
            launcher_.handle(),
            L"HLaunch 屏幕边缘",
            L"无法启动屏幕边缘唤起。",
            ui::TaskDialogIcon::Warning,
            L"该功能已保持关闭，请检查显示器状态后重新启动 HLaunch。");
    }
    if (!trayStarted) {
        ui::showTaskMessage(
            launcher_.handle(),
            L"HLaunch 托盘",
            L"无法创建托盘图标。",
            ui::TaskDialogIcon::Warning,
            L"快捷键仍可使用；请重新启动 Explorer 或 HLaunch 后重试。");
    }

    MSG message{};
    BOOL messageResult{};
    while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (settings_.isVisible()
            && IsDialogMessageW(settings_.handle(), &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (messageResult == -1) {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Error,
            "message_loop_failed",
            GetLastError());
        return 7;
    }
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "message_loop_stopped exit_code=" + std::to_string(message.wParam));
    return static_cast<int>(message.wParam);
}

bool Application::createActivationWindow(const HINSTANCE instance)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = &Application::activationWindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = platform::windows::activationWindowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    activationWindow_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        platform::windows::activationWindowClassName,
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
    return activationWindow_ != nullptr;
}

LRESULT CALLBACK Application::activationWindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    Application* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam); // NOLINT(performance-no-int-to-ptr): Win32 LPARAM carries this pointer.
        self = static_cast<Application*>(create->lpCreateParams);
        self->activationWindow_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else {
        self = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr): Win32 stores this pointer as LONG_PTR.
    }

    if (self) {
        return self->handleActivationMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::handleActivationMessage(
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    if (message == platform::windows::activationMessageId()) {
        const auto command = static_cast<platform::windows::ActivationCommand>(wParam);
        execute(command);
        return 0;
    }
    if (message == itemsSaveFailedMessage) {
        ui::showTaskMessage(
            launcher_.handle(),
            L"HLaunch 保存失败",
            L"条目无法保存到 items.json。",
            ui::TaskDialogIcon::Error,
            L"条目已在当前会话中更新。请检查数据目录权限或磁盘空间后重试。");
        return 0;
    }
    if (message == WM_HOTKEY && hotkey_.handlesMessage(wParam)) {
        execute(platform::windows::ActivationCommand::Toggle);
        return 0;
    }
    if (message == platform::windows::screenEdgeActivationMessage) {
        if (const auto activation = screenEdge_.takePendingActivation()) {
            launcher_.showAtScreenEdge(*activation);
        }
        return 0;
    }
    if ((message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE)
        && screenEdge_.isRunning()) {
        screenEdge_.refreshMonitors();
    }
    if (ui::isSystemAppearanceMessage(message)) {
        launcher_.refreshSystemAppearance();
        settings_.refreshSystemAppearance();
    }
    if (const auto trayCommand = trayIcon_.handleMessage(
            message,
            wParam,
            lParam,
            launcher_.isVisible())) {
        switch (*trayCommand) {
        case platform::windows::TrayCommand::ToggleLauncher:
            launcher_.toggle();
            break;
        case platform::windows::TrayCommand::Settings:
            showSettings();
            break;
        case platform::windows::TrayCommand::Exit:
            launcher_.close();
            break;
        }
        return 0;
    }
    return DefWindowProcW(activationWindow_, message, wParam, lParam);
}

void Application::execute(const platform::windows::ActivationCommand command)
{
    infrastructure::logging::write(
        infrastructure::logging::Level::Debug,
        "activation command=" + std::string{activationCommandName(command)});
    switch (command) {
    case platform::windows::ActivationCommand::Show:
        launcher_.show();
        break;
    case platform::windows::ActivationCommand::Hide:
        launcher_.hide();
        break;
    case platform::windows::ActivationCommand::Toggle:
        launcher_.toggle();
        break;
    }
}

void Application::launch(const core::LaunchItem& item)
{
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "item_launch_started type=" + std::to_string(static_cast<unsigned int>(item.type)));
    const auto result = platform::windows::launchItem(launcher_.handle(), item);
    if (result) {
        infrastructure::logging::write(
            infrastructure::logging::Level::Info,
            "item_launch_succeeded");
        launcher_.hide();
        return;
    }
    if (result.error().code == platform::windows::ShellLaunchErrorCode::Cancelled) {
        infrastructure::logging::write(
            infrastructure::logging::Level::Warning,
            "item_launch_cancelled");
        return;
    }

    infrastructure::logging::writeSystemError(
        infrastructure::logging::Level::Error,
        "item_launch_failed error_code="
            + std::to_string(static_cast<unsigned int>(result.error().code)),
        result.error().systemCode);

    std::wstring message = result.error().code
        == platform::windows::ShellLaunchErrorCode::InvalidUtf8
        ? L"条目包含无效文本，无法启动。"
        : L"Windows 无法启动该条目。";
    if (result.error().systemCode != 0) {
        message += L"\n\n系统错误码：";
        message += std::to_wstring(result.error().systemCode);
    }
    ui::showTaskMessage(
        launcher_.handle(),
        L"HLaunch 启动失败",
        L"Windows 无法启动该条目。",
        ui::TaskDialogIcon::Error,
        message);
}

void Application::showSettings()
{
    std::expected<bool, std::wstring> startupEnabled{false};
    const auto startupState = platform::windows::isStartupEnabled();
    if (startupState) {
        startupEnabled = *startupState;
    }
    else {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Warning,
            "startup_registration_query_failed",
            startupState.error().systemCode);
        startupEnabled = std::unexpected(
            L"无法读取当前用户开机启动状态，系统错误码："
            + std::to_wstring(startupState.error().systemCode));
    }
    if (!settings_.show(
            instance_,
            launcher_.handle(),
            config_.appearance,
            config_.activation,
            std::move(startupEnabled),
            [this](const core::AppearanceConfig& appearance) {
                return changeAppearance(appearance);
            },
            [this](const core::ActivationConfig& activation) {
                return changeActivation(activation);
            },
            [this](const bool enabled) {
                return changeStartup(enabled);
            })) {
        ui::showTaskMessage(
            launcher_.handle(),
            L"HLaunch 设置",
            L"无法创建设置窗口。",
            ui::TaskDialogIcon::Error);
    }
}

std::expected<void, std::wstring> Application::changeStartup(const bool enabled)
{
    const auto result = platform::windows::setStartupEnabled(
        executablePath_, forcePortable_, enabled);
    if (!result) {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Error,
            "startup_registration_change_failed",
            result.error().systemCode);
        return std::unexpected(
            L"无法更新开机启动设置，系统错误码："
            + std::to_wstring(result.error().systemCode));
    }
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        enabled ? "startup_registration_enabled" : "startup_registration_disabled");
    return {};
}

std::expected<void, std::wstring> Application::changeActivation(
    const core::ActivationConfig& activation)
{
    if (config_.activation == activation) {
        return {};
    }

    auto updated = config_;
    updated.activation = activation;
    if (!core::validateConfig(updated).empty()) {
        return std::unexpected(L"激活设置无效，请检查快捷键组合。");
    }

    const auto previous = config_.activation;
    const auto hotkeyResult = hotkey_.apply(activationWindow_, activation.hotkey);
    if (!hotkeyResult) {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Warning,
            "hotkey_settings_apply_failed",
            hotkeyResult.error().systemCode);
        if (hotkeyResult.error().code
            == platform::windows::HotkeyErrorCode::RegistrationFailed) {
            return std::unexpected(L"快捷键被系统或其他程序占用，已保留原设置。");
        }
        return std::unexpected(L"无法更新快捷键，已保留原设置。");
    }

    const auto targets = platform::windows::ScreenEdgeActivationTargets{
        .activationWindow = activationWindow_,
        .launcherWindow = launcher_.handle(),
    };
    if (!screenEdge_.start(targets, activation.screenEdge)) {
        static_cast<void>(hotkey_.apply(activationWindow_, previous.hotkey));
        static_cast<void>(screenEdge_.start(targets, previous.screenEdge));
        infrastructure::logging::write(
            infrastructure::logging::Level::Warning,
            "screen_edge_settings_apply_failed");
        return std::unexpected(L"无法启用屏幕边缘唤起，已恢复原设置。");
    }

    const auto saved = infrastructure::filesystem::saveConfig(configFile_, updated);
    if (!saved) {
        const auto hotkeyRestored = hotkey_.apply(activationWindow_, previous.hotkey);
        const bool edgeRestored = screenEdge_.start(targets, previous.screenEdge);
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Error,
            "config_activation_save_failed",
            saved.error().systemCode);
        if (!hotkeyRestored || !edgeRestored) {
            infrastructure::logging::write(
                infrastructure::logging::Level::Error,
                "activation_settings_rollback_failed");
        }
        return std::unexpected(L"无法保存激活设置，已恢复原设置。");
    }

    config_ = std::move(updated);
    settings_.setActivation(config_.activation);
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "activation_settings_changed hotkey="
            + std::string{config_.activation.hotkey.enabled ? "enabled" : "disabled"}
            + " screen_edge="
            + std::string{config_.activation.screenEdge.enabled ? "enabled" : "disabled"});
    return {};
}

bool Application::changeAppearance(const core::AppearanceConfig& appearance)
{
    if (config_.appearance == appearance) {
        return true;
    }
    auto updated = config_;
    updated.appearance = appearance;
    const auto saved = infrastructure::filesystem::saveConfig(configFile_, updated);
    if (!saved) {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Error,
            "config_appearance_save_failed",
            saved.error().systemCode);
        ui::showTaskMessage(
            settings_.handle(),
            L"HLaunch 设置",
            L"无法保存外观设置。",
            ui::TaskDialogIcon::Error,
            L"请检查数据目录权限或磁盘空间。");
        return false;
    }
    config_ = std::move(updated);
    windowEffects_ = windowEffectsFromAppearance(config_.appearance);
    launcher_.setThemeMode(config_.appearance.theme);
    launcher_.setWindowEffects(windowEffects_);
    settings_.setAppearance(config_.appearance);
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "appearance_changed theme="
            + std::string{config_.appearance.theme == core::ThemeMode::Light
                    ? "light"
                    : "dark"}
            + " opacity=" + std::to_string(config_.appearance.opacityPercent));
    return true;
}

} // namespace hlaunch::app
