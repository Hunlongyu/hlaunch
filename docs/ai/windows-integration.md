# Windows 集成

当前实现状态（2026-08-26）：已实现按当前用户 SID 隔离的 Named Mutex、固定类名隐藏激活窗口、`--show`、`--hide`、`--toggle` 的注册消息转发、全局快捷键注册，以及 `Shell_NotifyIconW` 托盘图标。快捷键可用时主实例无参数启动默认隐藏；快捷键禁用或注册失败且没有显式启动命令时暂时显示主窗。托盘支持显示/隐藏和退出，收到 `TaskbarCreated` 后重新添加；设置入口在设置页实现前保持禁用。同一完整性级别下实测第二实例退出且主实例保持唯一。有限重试、不同完整性级别 UIPI 和开机启动仍未实现。

## Shell 启动与图标

- 使用 `ShellExecuteExW` 启动应用、文件、文件夹和 URL；管理员启动使用 `runas` verb 并处理用户取消 UAC。
- `.lnk` 通过 `IShellLinkW`/`IPersistFile` 解析；图标可使用 `IShellItemImageFactory`、`IExtractIconW` 或 `SHGetFileInfoW`，具体选择封装在图标服务中。
- 不拼接并执行 `cmd.exe /c`。目标、参数和工作目录分别传递，错误转换为可显示的领域错误。
- V1 不主动枚举 Packaged App/UWP；用户已有的可启动 `.lnk` 仍可作为普通快捷方式导入。

## OLE 拖放与 COM

UI 主线程必须调用 `OleInitialize(nullptr)` 并在退出时成对 `OleUninitialize()`。`winrt::init_apartment` 不能替代 OLE 拖放初始化；仅使用 `winrt::com_ptr` 也不要求额外初始化 WinRT。若以后真正调用 WinRT API，应另行设计兼容 STA 的组合初始化并平衡每次成功调用，不能让两个 RAII 所有者无意重复管理同一 apartment。

使用 `IDropTarget`、`IDataObject`、`RegisterDragDrop` 和 `RevokeDragDrop`。后台线程若调用 COM/Shell API，必须自行初始化合适 apartment，并保证接口按 COM 规则跨线程传递。

COM 接口默认用 `winrt::com_ptr`；HANDLE、HKEY、HICON、HMENU 等经典资源用 WIL RAII。

## 单实例与 IPC

- 主实例用 Named Mutex 持有进程级所有权；Mutex 生命周期覆盖整个应用。Mutex 名称按当前用户隔离。
- 主实例创建类名固定为 `HLaunch.ActivationWindow.v1` 的隐藏顶层窗口。第二实例解析 `--show`、`--hide` 或 `--toggle` 后，用 `FindWindowW` 找到该窗口并通过注册窗口消息通知主实例。
- 注册消息只能承载小型命令标识，不传指针或复杂数据。窗口发现、同用户隔离及不同完整性级别下的 UIPI 行为必须运行验证。
- 找不到主窗口时不应立即抢占仍被 Mutex 持有的实例；提供有限重试和明确错误。若 UIPI 因完整性级别不同阻止消息，提示用户以同一权限级别运行，不放宽消息过滤器。
- 未来需要结构化 IPC 时再使用 Named Pipe，并设计认证和协议版本。

## 托盘

使用 `Shell_NotifyIconW`。托盘菜单至少提供显示/隐藏、设置和退出。收到 `TaskbarCreated` 后重新注册图标；退出时删除图标。

当前实现覆盖图标添加/删除、Explorer 重启消息恢复、左键选择 Toggle，以及受工作区约束的显示/隐藏和退出菜单。设置项已显示但禁用，必须在真实设置页可打开后才能认为托盘契约完整。

## 开机启动

使用当前用户的 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，不要求管理员权限。命令行必须正确引用 EXE 路径；只有用户明确选择本次强制便携模式时才附加 `--portable`，通常由 EXE 旁的 `portable.flag` 决定。仅在用户主动启用时写入；禁用时删除属于 HLaunch 的值。

## 显示器与 DPI

- Manifest 声明 Per-Monitor DPI Awareness V2。
- 处理 `WM_DPICHANGED` 与显示拓扑变化，使用系统建议矩形并重新布局、重建需要按 DPI 缩放的资源。
- 内部布局使用 DIP，Win32 边界使用物理像素，转换集中在 DPI 服务。
- 窗口定位使用工作区避免遮挡任务栏；热区判断使用显示器边界。

## 全屏与进程信息

全屏检测与边缘抑制见 `activation-edge.md`。访问前台进程信息可能失败，必须按最小权限打开句柄，并把权限不足视为可恢复状态。
