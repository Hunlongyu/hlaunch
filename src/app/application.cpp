#include "app/application.h"

#include "infrastructure/filesystem/data_paths.h"
#include "infrastructure/filesystem/data_store.h"

#include <Ole2.h>
#include <wil/resource.h>

#include <array>
#include <filesystem>
#include <string>

namespace hlaunch::app {
namespace {

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
    MessageBoxW(nullptr, message, L"HLaunch", MB_OK | MB_ICONERROR);
}

} // namespace

int Application::run(const HINSTANCE instance, const StartupOptions& options)
{
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
        .executablePath = executablePath(),
        .forcePortable = options.portable,
    });
    if (!paths) {
        showStartupError(L"无法解析 HLaunch 数据目录。");
        return 4;
    }

    // Load now to exercise the storage boundary. The visual-only scaffold does not yet
    // bind these models to interactive controls.
    const auto config = infrastructure::filesystem::loadConfig(paths->configFile);
    const auto items = infrastructure::filesystem::loadItems(paths->itemsFile);
    if (!config || !items) {
        showStartupError(L"无法读取 HLaunch 数据文件。");
        return 5;
    }

    if (!launcher_.create(instance, options.windowEffects, options.showSearch)
        || !createActivationWindow(instance)) {
        showStartupError(L"无法创建 HLaunch 窗口。");
        return 6;
    }

    // Until hotkey and tray stages exist, showing on first launch keeps the UI scaffold
    // reachable and gives the user a normal close path.
    execute(options.activation.value_or(platform::windows::ActivationCommand::Show));

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
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
    return DefWindowProcW(activationWindow_, message, wParam, lParam);
}

void Application::execute(const platform::windows::ActivationCommand command)
{
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

} // namespace hlaunch::app
