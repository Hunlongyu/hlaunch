# Windows 集成

当前实现状态（2026-08-28）：已实现稳定的进程 AppUserModelID `Hunlongyu.HLaunch`、按当前用户 SID 隔离的 Named Mutex、固定类名隐藏激活窗口、`--show`、`--hide`、`--toggle` 的注册消息转发、第二实例有限重试、全局快捷键注册、`Shell_NotifyIconW` 托盘图标、条目的 `ShellExecuteExW` 启动适配、Launcher 的 OLE `IDropTarget` 注册与撤销，以及当前用户开机启动设置。快捷键可用时主实例无参数启动默认隐藏；快捷键禁用或注册失败且没有显式启动命令时暂时显示主窗。Launcher 关闭按钮只执行隐藏；未置顶且前台切换到其他进程后延迟收起，同进程的搜索、菜单和原生对话框不会触发误收起。运行期窗口置顶可由标题区或 `Ctrl+Space` 切换，启用时使用 `HWND_TOPMOST` 并阻止失焦收起，取消时恢复 `HWND_NOTOPMOST`。托盘支持显示/隐藏、打开可复用的原生设置窗和退出，收到 `TaskbarCreated` 后重新添加。同一完整性级别下实测第二实例退出且主实例保持唯一。Shell 适配保持目标、逻辑参数和工作目录分离，支持 `open`/`runas` 并区分 UAC 取消；平台路径策略统一展开环境变量并以 EXE 目录解析条目相对路径，URL 目标保持原文，启动、图标、所在位置和复制命令不依赖当前工作目录。`.lnk` 通过 `IShellLinkW`/`IPersistFile` 解析目标、参数、工作目录和显示方式；解析失败时仍交由 Shell 直接打开原快捷方式，以保留特殊 Shell/Packaged App 快捷方式兼容。条目菜单可为单次启动强制使用 `runas`、按相同引用规则复制完整命令，并通过 `SHOpenFolderAndSelectItems` 在 Explorer 中选中非 URL 目标。拖放接收 `CF_HDROP`、浏览器 URL 剪贴板格式和 Unicode URL 文本，文件属性与 URL 分类在后台完成，结果通过窗口消息回到 UI 线程。Shell 图标服务直接解码显式 `.ico`、`.png` 和 `.svg`，从 EXE/DLL 提取图标资源，未设置或解码失败时从目标取得系统图标；结果在后台统一转换成预乘 BGRA 像素并回送 UI。不同完整性级别 UIPI 完整矩阵仍需人工验证。

AppUserModelID 必须在创建 Launcher、搜索窗和托盘图标之前设置。失败只记录 HRESULT
并继续运行，不能让任务栏身份能力成为启动阻断项。该 ID 是 Shell 身份契约，后续
创建快捷方式、Jump List 或通知时必须复用，不得按安装路径或版本变化。

## Shell 启动与图标

- 使用 `ShellExecuteExW` 启动应用、文件、文件夹和 URL；管理员启动使用 `runas` verb 并处理用户取消 UAC。
- `.lnk` 通过 `IShellLinkW`/`IPersistFile` 解析目标、快捷方式参数、工作目录和显示方式；HLaunch 条目参数追加在快捷方式参数之后，条目显式工作目录优先。解析失败时回退为直接 Shell 打开原 `.lnk`。图标可使用 `IShellItemImageFactory`、`IExtractIconW` 或 `SHGetFileInfoW`，具体选择封装在图标服务中。
- 不拼接并执行 `cmd.exe /c`。目标、参数和工作目录分别传递，错误转换为可显示的领域错误。
- 文件系统路径在调用 Shell 或图标服务前统一展开环境变量；普通相对路径以 HLaunch EXE 目录为基准。拒绝 `C:relative` 和 `\\root-relative` 这类依赖进程状态的 Windows 路径。
- V1 不主动枚举 Packaged App/UWP；用户已有的可启动 `.lnk` 仍可作为普通快捷方式导入。

Shell 图标转换结果使用数据根 `cache/icons` 中的私有二进制缓存。读取前校验格式版本、完整身份、像素尺寸、文件长度和内容校验和；缓存键还包含源文件时间与大小。改变解码、Alpha 或缩放语义时必须提升缓存格式版本，使旧像素自动失效而不能继续显示。损坏或过期条目直接重新提取，缓存最多保留 256 项或 64 MiB，淘汰只处理 `.hlci` 文件。

## OLE 拖放与 COM

UI 主线程必须调用 `OleInitialize(nullptr)` 并在退出时成对 `OleUninitialize()`。`winrt::init_apartment` 不能替代 OLE 拖放初始化；仅使用 `winrt::com_ptr` 也不要求额外初始化 WinRT。若以后真正调用 WinRT API，应另行设计兼容 STA 的组合初始化并平衡每次成功调用，不能让两个 RAII 所有者无意重复管理同一 apartment。

使用 `IDropTarget`、`IDataObject`、`RegisterDragDrop` 和 `RevokeDragDrop`。后台线程若调用 COM/Shell API，必须自行初始化合适 apartment，并保证接口按 COM 规则跨线程传递。

当前实现仅在 UI 线程读取 `IDataObject` 并复制路径或 URL 字符串，不把 COM 接口跨线程传递。文件系统路径落在已有应用或快捷方式 Grid 条目时，保留该条目已配置的逻辑参数，再按拖入顺序把每个路径追加为一次性逻辑参数并立即启动；不修改或新增持久化条目。落在空槽、非程序条目、Tab 或其他区域时，后台解析器按来源顺序生成领域条目，并保留 UI 线程命中的目标 Tab 与可选 `gridSlot`；Grid 精确槽位连续导入时，已占用的连续槽位向后顺移，Tab 或其他区域则追加到对应分类末尾。浏览器 URL 拖入仍用于添加 URL 条目，脚本式 URL scheme（`javascript:`、`vbscript:`、`data:`）拒绝导入。OLE 数据对象集成测试覆盖文件顺序、URL、复制效果和不支持格式；Explorer 与主流浏览器的真实鼠标拖入矩阵仍需人工验证。

COM 接口默认用 `winrt::com_ptr`；HANDLE、HKEY、HICON、HMENU 等经典资源用 WIL RAII。

## 单实例与 IPC

- 主实例用 Named Mutex 持有进程级所有权；Mutex 生命周期覆盖整个应用。Mutex 名称按当前用户隔离。
- 主实例创建类名固定为 `HLaunch.ActivationWindow.v1` 的隐藏顶层窗口。第二实例解析 `--show`、`--hide` 或 `--toggle` 后，最多用 `FindWindowW` 重试 20 次、每次间隔 50 ms，再通过带 1 秒超时的注册窗口消息通知主实例。
- 注册消息只能承载小型命令标识，不传指针或复杂数据。窗口发现、同用户隔离及不同完整性级别下的 UIPI 行为必须运行验证。
- 找不到主窗口时不立即抢占仍被 Mutex 持有的实例；有限重试耗尽后明确提示主实例尚未就绪。消息投递失败时区分访问被拒绝、主实例无响应和其他系统错误。若 UIPI 因完整性级别不同阻止消息，提示用户以同一权限级别运行，不放宽消息过滤器。
- 未来需要结构化 IPC 时再使用 Named Pipe，并设计认证和协议版本。

## 托盘

使用 `Shell_NotifyIconW`。托盘悬浮提示固定为两行：第一行显示 `HLaunch`，第二行以 `v{major.minor.patch}` 格式显示由 CMake 项目版本提供的当前版本号，例如 `v0.1.1`。托盘菜单至少提供显示/隐藏、设置和退出。收到 `TaskbarCreated` 后重新注册图标；退出时删除图标。

当前实现覆盖图标添加/删除、Explorer 重启消息恢复、左键选择 Toggle，以及受工作区约束的显示/隐藏、设置和退出菜单。设置使用可复用的标准 Win32 模型对话框，覆盖外观、快捷键、边缘唤起和开机启动；消息循环通过 `IsDialogMessageW` 保留标准 Tab 与助记键行为。

## 开机启动

使用当前用户的 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，不要求管理员权限。命令行必须正确引用 EXE 路径；数据目录默认已经便携优先，因此新命令不依赖 `portable.flag`。若用户通过旧命令显式传入 `--portable`，重写开机启动项时继续保留该参数以兼容既有调用，但数据目录无权限时仍允许回退 AppData。仅在用户主动启用时写入；禁用时删除属于 HLaunch 的值。

当前实现以固定 `HLaunch` 注册表值作为状态真相，不在 `config.json` 重复保存。设置窗打开时读取该值；启用时始终用当前 EXE 绝对路径重写命令，可修复程序移动后的旧路径；禁用时只删除该固定值，不影响同一 Run 键中的其他程序。查询或写入失败时记录系统错误码并在设置窗就地反馈。

## 显示器与 DPI

- Manifest 声明 Per-Monitor DPI Awareness V2。
- 处理 `WM_DPICHANGED` 与显示拓扑变化，使用系统建议矩形并重新布局、重建需要按 DPI 缩放的资源。
- 内部布局使用 DIP，Win32 边界使用物理像素，转换集中在 DPI 服务。
- 窗口定位使用工作区避免遮挡任务栏；热区判断使用显示器边界。

## 全屏与进程信息

全屏检测与边缘抑制见 `activation-edge.md`。访问前台进程信息可能失败，必须按最小权限打开句柄，并把权限不足视为可恢复状态。
