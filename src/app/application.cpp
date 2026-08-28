#include "app/application.h"

#include "core/data_validation.h"
#include "infrastructure/filesystem/data_paths.h"
#include "infrastructure/filesystem/data_store.h"
#include "infrastructure/logging/diagnostic_log.h"
#include "platform/windows/app_identity.h"
#include "platform/windows/shell_launcher.h"
#include "platform/windows/startup_registration.h"
#include "resource.h"
#include "ui/system_appearance.h"
#include "ui/task_dialog.h"

#include <Ole2.h>
#include <shellapi.h>
#include <wil/resource.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hlaunch::app {
namespace {

constexpr UINT itemsSaveFailedMessage = WM_APP + 0x41U;
constexpr UINT configSaveCompletedMessage = WM_APP + 0x42U;

std::filesystem::path executablePath()
{
    std::wstring buffer(32'768, L'\0');
    const auto length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
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
    } else {
        message = L"无法注册配置的全局快捷键，快捷键未启用。\n\n"
                  L"该组合可能已被系统或其他程序占用。请关闭占用程序后重新启动 HLaunch。"
                  L"\n\n系统错误码：";
        message += std::to_wstring(error.systemCode);
    }
    ui::showTaskMessage(owner, L"HLaunch 快捷键", L"全局快捷键不可用。",
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

std::string_view activationCommandName(const platform::windows::ActivationCommand command) noexcept
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

platform::windows::WindowEffects
windowEffectsFromAppearance(const core::AppearanceConfig& appearance) noexcept
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
        const auto command =
            options.activation.value_or(platform::windows::ActivationCommand::Show);
        const auto notified = platform::windows::notifyPrimaryInstance(command);
        if (notified) {
            return 0;
        }
        if (notified.error().code
            == platform::windows::PrimaryNotificationErrorCode::WindowNotFound) {
            showStartupError(
                L"另一个 HLaunch 实例正在启动，但其窗口尚未就绪。请稍后重试。");
        }
        else if (notified.error().systemCode == ERROR_ACCESS_DENIED) {
            showStartupError(
                L"无法通知已运行的 HLaunch。请确保两个实例使用相同的管理员权限运行。");
        }
        else {
            showStartupError(L"已运行的 HLaunch 暂时没有响应，请稍后重试。");
        }
        return 2;
    }

    const auto oleResult = OleInitialize(nullptr);
    if (FAILED(oleResult)) {
        showStartupError(L"OLE 初始化失败，HLaunch 无法启动。");
        return 3;
    }
    const auto oleCleanup = wil::scope_exit([] { OleUninitialize(); });

    const auto paths = infrastructure::filesystem::resolveDataPaths({
        .executablePath = executablePath_,
    });
    if (!paths) {
        showStartupError(
            L"无法创建可读写的 HLaunch 数据目录。已尝试软件同目录的 data 和当前用户 AppData。"
            L"请检查目录权限或磁盘空间后重试。");
        return 4;
    }

    const auto config = infrastructure::filesystem::loadConfig(paths->configFile);
    if (!config || !config->value) {
        OutputDebugStringW(L"HLaunch could not load config.json.\n");
        showStartupError(L"无法读取 HLaunch 配置文件。");
        return 5;
    }
    config_ = *config->value;
    persistedConfig_ = config_;
    logDirectory_ = paths->logDirectory;

    if (config_.diagnostics.loggingEnabled && applyDiagnosticLogging(true)) {
        infrastructure::logging::write(infrastructure::logging::Level::Info,
                                       paths->portable ? "application_started mode=portable"
                                                       : "application_started mode=standard");
    } else if (config_.diagnostics.loggingEnabled) {
        OutputDebugStringW(L"HLaunch could not initialize diagnostic logging.\n");
    }
    const auto logCleanup = wil::scope_exit([this] {
        if (diagnosticLoggingActive_) {
            infrastructure::logging::write(
                infrastructure::logging::Level::Info,
                "application_stopped");
        }
        infrastructure::logging::shutdown();
        diagnosticLoggingActive_ = false;
    });

    const auto identityResult = platform::windows::setProcessAppUserModelId();
    if (identityResult) {
        infrastructure::logging::write(infrastructure::logging::Level::Info,
                                       "app_user_model_id_set value=Hunlongyu.HLaunch");
    } else {
        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Warning, "app_user_model_id_set_failed",
            static_cast<unsigned long>(identityResult.error()));
    }

    auto items = infrastructure::filesystem::loadItems(paths->itemsFile);
    if (!items || !items->value) {
        if (!items) {
            infrastructure::logging::writeSystemError(infrastructure::logging::Level::Error,
                                                      "items_load_failed",
                                                      items.error().systemCode);
        } else if (!items->value) {
            infrastructure::logging::write(infrastructure::logging::Level::Error,
                                           "items_load_rejected issue_count=" +
                                               std::to_string(items->issues.size()));
        }
        showStartupError(L"无法读取 HLaunch 数据文件。");
        return 5;
    }

    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "config_loaded source=" + std::string{loadSourceName(config->source)} +
            " issue_count=" + std::to_string(config->issues.size()));
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "items_loaded source=" + std::string{loadSourceName(items->source)} +
            " tab_count=" + std::to_string(items->value->tabs.size()) +
            " item_count=" + std::to_string(itemCount(*items->value)) +
            " issue_count=" + std::to_string(items->issues.size()));

    windowEffects_ = windowEffectsFromAppearance(config_.appearance);
    if (options.backdropSpecified) {
        windowEffects_.backdrop = options.windowEffects.backdrop;
    }
    if (options.opacitySpecified) {
        windowEffects_.opacityPercent = options.windowEffects.opacityPercent;
    }

    infrastructure::logging::write(infrastructure::logging::Level::Info,
                                   "launcher_window_create_started");
    if (!launcher_.create(
            instance, windowEffects_, options.showSearch, std::move(*items->value),
            [this](const core::LaunchItem& item) { launch(item); }, {},
            paths->iconCacheDirectory,
            {
                .executableDirectory = executablePath_.parent_path(),
                .portable = paths->portable,
            },
            config_.appearance.gridColumns, config_.appearance.gridRows, {})) {
        infrastructure::logging::writeSystemError(infrastructure::logging::Level::Error,
                                                  "launcher_window_create_failed", GetLastError());
        showStartupError(L"无法创建 HLaunch 窗口。");
        return 6;
    }
    infrastructure::logging::write(infrastructure::logging::Level::Info, "launcher_window_created");

    if (!createActivationWindow(instance)) {
        infrastructure::logging::writeSystemError(infrastructure::logging::Level::Error,
                                                  "activation_window_create_failed",
                                                  GetLastError());
        showStartupError(L"无法创建 HLaunch 窗口。");
        return 6;
    }
    infrastructure::logging::write(infrastructure::logging::Level::Info,
                                   "activation_window_created");
    launcher_.setSettingsHandler([this] { showSettings(); });

    try {
        const HWND notificationWindow = activationWindow_;
        configSaver_.emplace(
            paths->configFile,
            [this, notificationWindow](
                infrastructure::filesystem::ConfigSaveCompletion completion) {
                {
                    const std::scoped_lock lock{configSaveCompletionMutex_};
                    configSaveCompletions_.push_back(std::move(completion));
                }
                if (!PostMessageW(notificationWindow, configSaveCompletedMessage, 0, 0)) {
                    infrastructure::logging::writeSystemError(
                        infrastructure::logging::Level::Error,
                        "config_save_completion_post_failed",
                        GetLastError());
                }
            });
    } catch (const std::exception&) {
        infrastructure::logging::write(infrastructure::logging::Level::Error,
                                       "config_save_worker_create_failed");
        showStartupError(L"无法启动配置保存服务。");
        return 6;
    }
    const auto configSaverCleanup = wil::scope_exit([this] { configSaver_.reset(); });

    try {
        itemsSaver_.emplace(
            paths->itemsFile, [this](const infrastructure::filesystem::StoreError& error) {
                infrastructure::logging::writeSystemError(infrastructure::logging::Level::Error,
                                                          "items_save_failed", error.systemCode);
                if (activationWindow_) {
                    PostMessageW(activationWindow_, itemsSaveFailedMessage, 0, 0);
                }
            });
    } catch (const std::exception&) {
        infrastructure::logging::write(infrastructure::logging::Level::Error,
                                       "items_save_worker_create_failed");
        showStartupError(L"无法启动条目保存服务。");
        return 6;
    }
    const auto itemsSaverCleanup = wil::scope_exit([this] { itemsSaver_.reset(); });

    launcher_.setDocumentChangedHandler([this](const core::ItemsDocument& document) {
        if (itemsSaver_) {
            itemsSaver_->submit(document);
        }
    });
    launcher_.setGridSizeChangedHandler(
        [this](const std::uint16_t columns, const std::uint16_t rows) {
            auto appearance = config_.appearance;
            appearance.gridColumns = columns;
            appearance.gridRows = rows;
            return changeAppearance(appearance);
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
    const bool trayStarted =
        trayIcon_.start(activationWindow_, LoadIconW(instance, MAKEINTRESOURCEW(IDI_HLAUNCH)));

    if (hotkeyResult) {
        infrastructure::logging::write(infrastructure::logging::Level::Info,
                                       hotkeyAvailable ? "hotkey_ready" : "hotkey_disabled");
    } else {
        infrastructure::logging::writeSystemError(infrastructure::logging::Level::Warning,
                                                  "hotkey_registration_failed",
                                                  hotkeyResult.error().systemCode);
    }
    infrastructure::logging::write(screenEdgeStarted ? infrastructure::logging::Level::Info
                                                     : infrastructure::logging::Level::Warning,
                                   screenEdgeStarted ? "screen_edge_service_ready"
                                                     : "screen_edge_service_failed");
    infrastructure::logging::write(trayStarted ? infrastructure::logging::Level::Info
                                               : infrastructure::logging::Level::Warning,
                                   trayStarted ? "tray_icon_ready" : "tray_icon_failed");

    if (options.activation) {
        execute(*options.activation);
    } else if (options.showSearch || !hotkeyAvailable) {
        // Keep the application reachable while tray and settings UI are still
        // pending.
        execute(platform::windows::ActivationCommand::Show);
    }

    if (!hotkeyResult) {
        showHotkeyError(launcher_.handle(), hotkeyResult.error());
    }
    if (!screenEdgeStarted) {
        ui::showTaskMessage(launcher_.handle(), L"HLaunch 屏幕边缘", L"无法启动屏幕边缘唤起。",
                            ui::TaskDialogIcon::Warning,
                            L"该功能已保持关闭，请检查显示器状态后重新启动 HLaunch。");
    }
    if (!trayStarted) {
        ui::showTaskMessage(launcher_.handle(), L"HLaunch 托盘", L"无法创建托盘图标。",
                            ui::TaskDialogIcon::Warning,
                            L"快捷键仍可使用；请重新启动 Explorer 或 HLaunch 后重试。");
    }

    MSG message{};
    BOOL messageResult{};
    while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (settings_.isVisible() && IsDialogMessageW(settings_.handle(), &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (messageResult == -1) {
        infrastructure::logging::writeSystemError(infrastructure::logging::Level::Error,
                                                  "message_loop_failed", GetLastError());
        return 7;
    }
    infrastructure::logging::write(infrastructure::logging::Level::Info,
                                   "message_loop_stopped exit_code=" +
                                       std::to_string(message.wParam));
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
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, platform::windows::activationWindowClassName, L"",
        WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    return activationWindow_ != nullptr;
}

LRESULT CALLBACK Application::activationWindowProcedure(const HWND window, const UINT message,
                                                        const WPARAM wParam,
                                                        const LPARAM lParam) noexcept
{
    try {
        Application* self = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(
                lParam); // NOLINT(performance-no-int-to-ptr):
                         // Win32 LPARAM carries this pointer.
            self = static_cast<Application*>(create->lpCreateParams);
            self->activationWindow_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Application*>(
                GetWindowLongPtrW(window, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr):
                                                           // Win32 stores this pointer as LONG_PTR.
        }

        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            if (self && self->activationWindow_ == window)
                self->activationWindow_ = nullptr;
            return DefWindowProcW(window, message, wParam, lParam);
        }
        if (self) {
            return self->handleActivationMessage(message, wParam, lParam);
        }
    } catch (...) {
        infrastructure::logging::write(infrastructure::logging::Level::Error,
                                       "activation_window_callback_exception");
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::handleActivationMessage(const UINT message, const WPARAM wParam,
                                             const LPARAM lParam)
{
    if (message == platform::windows::activationMessageId()) {
        const auto command = static_cast<platform::windows::ActivationCommand>(wParam);
        execute(command);
        return 0;
    }
    if (message == itemsSaveFailedMessage) {
        ui::showTaskMessage(launcher_.handle(), L"HLaunch 保存失败", L"条目无法保存到 items.json。",
                            ui::TaskDialogIcon::Error,
                            L"条目已在当前会话中更新。请检查数据目录权限或磁盘空间后重试。");
        return 0;
    }
    if (message == configSaveCompletedMessage) {
        handleConfigSaveCompletions();
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
    if ((message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE) && screenEdge_.isRunning()) {
        screenEdge_.refreshMonitors();
    }
    if (ui::isSystemAppearanceMessage(message)) {
        launcher_.refreshSystemAppearance();
        settings_.refreshSystemAppearance();
    }
    if (const auto trayCommand =
            trayIcon_.handleMessage(message, wParam, lParam, launcher_.isVisible())) {
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
    infrastructure::logging::write(infrastructure::logging::Level::Debug,
                                   "activation command=" +
                                       std::string{activationCommandName(command)});
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
    infrastructure::logging::write(infrastructure::logging::Level::Info,
                                   "item_launch_started type=" +
                                       std::to_string(static_cast<unsigned int>(item.type)));
    const auto result = platform::windows::launchItem(launcher_.handle(), item);
    if (result) {
        infrastructure::logging::write(infrastructure::logging::Level::Info,
                                       "item_launch_succeeded");
        launcher_.recordSuccessfulLaunch(item.id);
        launcher_.hide();
        return;
    }
    if (result.error().code == platform::windows::ShellLaunchErrorCode::Cancelled) {
        infrastructure::logging::write(infrastructure::logging::Level::Warning,
                                       "item_launch_cancelled");
        return;
    }

    infrastructure::logging::writeSystemError(
        infrastructure::logging::Level::Error,
        "item_launch_failed error_code=" +
            std::to_string(static_cast<unsigned int>(result.error().code)),
        result.error().systemCode);

    std::wstring message =
        result.error().code == platform::windows::ShellLaunchErrorCode::InvalidUtf8
            ? L"条目包含无效文本，无法启动。"
            : L"Windows 无法启动该条目。";
    if (result.error().systemCode != 0) {
        message += L"\n\n系统错误码：";
        message += std::to_wstring(result.error().systemCode);
    }
    ui::showTaskMessage(launcher_.handle(), L"HLaunch 启动失败", L"Windows 无法启动该条目。",
                        ui::TaskDialogIcon::Error, message);
}

void Application::showSettings()
{
    std::expected<bool, std::wstring> startupEnabled{false};
    const auto startupState = platform::windows::isStartupEnabled();
    if (startupState) {
        startupEnabled = *startupState;
    } else {
        infrastructure::logging::writeSystemError(infrastructure::logging::Level::Warning,
                                                  "startup_registration_query_failed",
                                                  startupState.error().systemCode);
        startupEnabled = std::unexpected(L"无法读取当前用户开机启动状态，系统错误码：" +
                                         std::to_wstring(startupState.error().systemCode));
    }
    if (!settings_.show(
            instance_, launcher_.handle(), config_.appearance, config_.activation,
            std::move(startupEnabled),
            config_.diagnostics.loggingEnabled,
            [this](const core::AppearanceConfig& appearance) {
                return changeAppearance(appearance);
            },
            [this](const core::ActivationConfig& activation) {
                return changeActivation(activation);
            },
            [this](const bool enabled) { return changeStartup(enabled); },
            [this](const bool enabled) { return changeDiagnostics(enabled); })) {
        ui::showTaskMessage(launcher_.handle(), L"HLaunch 设置", L"无法创建设置窗口。",
                            ui::TaskDialogIcon::Error);
    }
}

std::expected<void, std::wstring> Application::changeStartup(const bool enabled)
{
    const auto result =
        platform::windows::setStartupEnabled(executablePath_, forcePortable_, enabled);
    if (!result) {
        infrastructure::logging::writeSystemError(infrastructure::logging::Level::Error,
                                                  "startup_registration_change_failed",
                                                  result.error().systemCode);
        return std::unexpected(L"无法更新开机启动设置，系统错误码：" +
                               std::to_wstring(result.error().systemCode));
    }
    infrastructure::logging::write(infrastructure::logging::Level::Info,
                                   enabled ? "startup_registration_enabled"
                                           : "startup_registration_disabled");
    return {};
}

std::expected<void, std::wstring> Application::changeDiagnostics(const bool enabled)
{
    if (config_.diagnostics.loggingEnabled == enabled
        && diagnosticLoggingActive_ == enabled) {
        return {};
    }

    const bool previousRuntimeState = diagnosticLoggingActive_;
    if (!applyDiagnosticLogging(enabled)) {
        return std::unexpected(
            enabled
                ? L"无法启用诊断日志，请检查数据目录权限或磁盘空间。"
                : L"无法关闭诊断日志。");
    }

    if (config_.diagnostics.loggingEnabled == enabled) {
        settings_.setDiagnosticLogging(enabled, L"诊断日志状态已更新。");
        return {};
    }

    auto updated = config_;
    updated.diagnostics.loggingEnabled = enabled;
    if (!submitConfigSnapshot(updated)) {
        static_cast<void>(applyDiagnosticLogging(previousRuntimeState));
        infrastructure::logging::write(
            infrastructure::logging::Level::Error,
            "config_diagnostics_save_submit_failed");
        return std::unexpected(L"无法保存日志设置，已恢复原状态。");
    }

    config_ = std::move(updated);
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        enabled ? "diagnostic_logging_enabled" : "diagnostic_logging_disabled");
    return {};
}

bool Application::applyDiagnosticLogging(const bool enabled)
{
    if (diagnosticLoggingActive_ == enabled) {
        return true;
    }
    if (!enabled) {
        infrastructure::logging::write(
            infrastructure::logging::Level::Info,
            "diagnostic_logging_stopped");
        infrastructure::logging::shutdown();
        diagnosticLoggingActive_ = false;
        return true;
    }

    const auto initialized = infrastructure::logging::initialize({
        .directory = logDirectory_,
    });
    if (!initialized) {
        return false;
    }
    infrastructure::logging::installUnhandledExceptionHandler();
    diagnosticLoggingActive_ = true;
    return true;
}

std::expected<void, std::wstring>
Application::changeActivation(const core::ActivationConfig& activation)
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
        infrastructure::logging::writeSystemError(infrastructure::logging::Level::Warning,
                                                  "hotkey_settings_apply_failed",
                                                  hotkeyResult.error().systemCode);
        if (hotkeyResult.error().code == platform::windows::HotkeyErrorCode::RegistrationFailed) {
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
        infrastructure::logging::write(infrastructure::logging::Level::Warning,
                                       "screen_edge_settings_apply_failed");
        return std::unexpected(L"无法启用屏幕边缘唤起，已恢复原设置。");
    }

    if (!submitConfigSnapshot(updated)) {
        const auto hotkeyRestored = hotkey_.apply(activationWindow_, previous.hotkey);
        const bool edgeRestored = screenEdge_.start(targets, previous.screenEdge);
        infrastructure::logging::write(infrastructure::logging::Level::Error,
                                       "config_activation_save_submit_failed");
        if (!hotkeyRestored || !edgeRestored) {
            infrastructure::logging::write(infrastructure::logging::Level::Error,
                                           "activation_settings_rollback_failed");
        }
        return std::unexpected(L"无法保存激活设置，已恢复原设置。");
    }

    config_ = std::move(updated);
    settings_.setActivation(config_.activation);
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "activation_settings_changed hotkey=" +
            std::string{config_.activation.hotkey.enabled ? "enabled" : "disabled"} +
            " screen_edge=" +
            std::string{config_.activation.screenEdge.enabled ? "enabled" : "disabled"});
    return {};
}

bool Application::changeAppearance(const core::AppearanceConfig& appearance)
{
    if (config_.appearance == appearance) {
        return true;
    }
    auto updated = config_;
    updated.appearance = appearance;
    if (!core::validateConfig(updated).empty() || !submitConfigSnapshot(updated)) {
        infrastructure::logging::write(infrastructure::logging::Level::Error,
                                       "config_appearance_save_submit_failed");
        ui::showTaskMessage(settings_.handle(), L"HLaunch 设置", L"无法保存窗口设置。",
                            ui::TaskDialogIcon::Error, L"请检查数据目录权限或磁盘空间。");
        return false;
    }
    config_ = std::move(updated);
    windowEffects_ = windowEffectsFromAppearance(config_.appearance);
    launcher_.setWindowEffects(windowEffects_);
    infrastructure::logging::write(
        infrastructure::logging::Level::Info,
        "appearance_changed opacity=" + std::to_string(config_.appearance.opacityPercent));
    return true;
}

bool Application::submitConfigSnapshot(core::ApplicationConfig snapshot)
{
    if (!configSaver_) {
        return false;
    }
    try {
        const auto revision = latestConfigRevision_ + 1U;
        if (!configSaver_->submit(revision, std::move(snapshot))) {
            return false;
        }
        latestConfigRevision_ = revision;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void Application::handleConfigSaveCompletions()
{
    std::deque<infrastructure::filesystem::ConfigSaveCompletion> completions{};
    {
        const std::scoped_lock lock{configSaveCompletionMutex_};
        completions.swap(configSaveCompletions_);
    }

    for (auto& completion : completions) {
        if (!completion.error) {
            const bool appearanceWasPersisted =
                completion.snapshot.appearance != persistedConfig_.appearance;
            const bool activationWasPersisted =
                completion.snapshot.activation != persistedConfig_.activation;
            const bool diagnosticsWasPersisted =
                completion.snapshot.diagnostics != persistedConfig_.diagnostics;
            if (completion.revision >= persistedConfigRevision_) {
                persistedConfig_ = completion.snapshot;
                persistedConfigRevision_ = completion.revision;
            }
            if (completion.revision == latestConfigRevision_
                && appearanceWasPersisted
                && completion.snapshot.appearance == config_.appearance) {
                settings_.setStatus(L"窗口透明度已保存并立即生效。");
            }
            if (completion.revision == latestConfigRevision_
                && activationWasPersisted
                && completion.snapshot.activation == config_.activation) {
                settings_.setActivationStatus(L"激活设置已保存并立即生效。");
            }
            if (completion.revision == latestConfigRevision_
                && diagnosticsWasPersisted
                && completion.snapshot.diagnostics == config_.diagnostics) {
                settings_.setDiagnosticLogging(
                    config_.diagnostics.loggingEnabled,
                    config_.diagnostics.loggingEnabled
                        ? L"诊断日志已启用并保存。"
                        : L"诊断日志已关闭并保存。");
            }
            continue;
        }

        infrastructure::logging::writeSystemError(
            infrastructure::logging::Level::Error,
            "config_save_failed revision=" + std::to_string(completion.revision),
            completion.error->systemCode);
        if (completion.revision != latestConfigRevision_) {
            continue;
        }

        const bool activationNeedsRollback =
            completion.snapshot.activation != persistedConfig_.activation
            && config_.activation == completion.snapshot.activation;
        const bool appearanceWasNotSaved =
            completion.snapshot.appearance != persistedConfig_.appearance
            && config_.appearance == completion.snapshot.appearance;
        const bool diagnosticsNeedsRollback =
            completion.snapshot.diagnostics != persistedConfig_.diagnostics
            && config_.diagnostics == completion.snapshot.diagnostics;
        bool activationRolledBack = false;
        if (activationNeedsRollback) {
            const auto targets = platform::windows::ScreenEdgeActivationTargets{
                .activationWindow = activationWindow_,
                .launcherWindow = launcher_.handle(),
            };
            const auto hotkeyRestored =
                hotkey_.apply(activationWindow_, persistedConfig_.activation.hotkey);
            const bool edgeRestored =
                screenEdge_.start(targets, persistedConfig_.activation.screenEdge);
            if (hotkeyRestored && edgeRestored) {
                config_.activation = persistedConfig_.activation;
                settings_.setActivation(config_.activation);
                settings_.setActivationStatus(L"保存失败，已恢复上次保存的激活设置。");
                activationRolledBack = true;
            } else {
                infrastructure::logging::write(
                    infrastructure::logging::Level::Error,
                    "activation_settings_async_rollback_failed");
                settings_.setActivationStatus(L"保存失败，且无法恢复激活设置。");
            }
        }

        bool diagnosticsRolledBack = false;
        if (diagnosticsNeedsRollback
            && applyDiagnosticLogging(persistedConfig_.diagnostics.loggingEnabled)) {
            config_.diagnostics = persistedConfig_.diagnostics;
            settings_.setDiagnosticLogging(
                config_.diagnostics.loggingEnabled,
                L"保存失败，已恢复上次保存的日志设置。");
            diagnosticsRolledBack = true;
        }
        else if (diagnosticsNeedsRollback) {
            settings_.setDiagnosticLogging(
                config_.diagnostics.loggingEnabled,
                L"保存失败，且无法恢复上次的日志设置。");
        }

        std::wstring details = L"请检查数据目录权限或磁盘空间。";
        if (activationRolledBack) {
            details += L"\n\n快捷键和屏幕边缘设置已恢复为上次保存值。";
        }
        if (appearanceWasNotSaved) {
            settings_.setStatus(L"窗口透明度已在本次会话生效，但配置保存失败。");
            details += L"\n\n当前窗口透明度或 Grid 尺寸仅在本次会话中有效，"
                       L"未写入 config.json。";
        }
        if (diagnosticsRolledBack) {
            details += L"\n\n诊断日志已恢复为上次保存状态。";
        }
        ui::showTaskMessage(
            settings_.isVisible() ? settings_.handle() : launcher_.handle(),
            L"HLaunch 保存失败",
            L"配置无法保存到 config.json。",
            ui::TaskDialogIcon::Error,
            details);
    }
}

} // namespace hlaunch::app
