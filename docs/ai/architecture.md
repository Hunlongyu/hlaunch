# 架构与边界

## 分层

```text
App
 ├─ UI ───────────────► Core
 ├─ Activation ───────► Core
 └─ Platform/Windows ─► Core

Graphics ◄──────── UI
Infrastructure ◄── App/Core/Platform
```

- `app`：进程生命周期、命令行、单实例、服务装配。
- `core`：Item、Tab、Search 和 Config 的领域模型与用例；不依赖 HWND、Direct2D 或 JSON 库类型。
- `ui`：Launcher、Grid、Tab、Search、Settings、Context Menu；负责输入、布局和呈现。
- `graphics`：Direct2D、DirectWrite、WIC、设备资源和缓存。
- `activation`：快捷键、边缘停留、热区、全屏判断；只产生 `ActivationContext`。
- `platform/windows`：Shell、OLE 拖放、注册表、托盘、DPI、显示器、进程和单实例协调。
- `infrastructure`：JSON、文件系统、日志与后台调度。

## 依赖规则

- UI 通过 Core 用例修改数据，不能直接写 JSON。
- Core 不包含 Win32 句柄或第三方库对象。
- Activation 不直接调用 `SetWindowPos`；由 UI 的定位服务消费激活上下文。
- 平台服务把 Win32/COM 错误转换成项目错误类型。
- 第三方库必须包在单一适配层后面。

## 激活数据流

```text
HotkeyTrigger / EdgeDwellTrigger
              │
              ▼
      ActivationManager
              │ ActivationContext
              ▼
    ActivationController
              │
        PopupPositionService
              │
              ▼
       LauncherWindow
```

`ActivationContext` 至少包含：触发类型、鼠标位置、目标显示器、热区和请求行为（show/toggle）。不要为尚未实现的触发器预留会被误当成支持范围的枚举值。HLaunch 不提供外部激活 API；托盘和第二实例只调用进程内已定义入口，不扩展为通用 IPC。

## 线程模型

- UI 主线程拥有消息循环、HWND、输入、布局、渲染和动画。
- 图标提取、磁盘 I/O 和较重 Shell 查询在后台执行。
- 后台任务不可直接操作 HWND、Direct2D 窗口资源或 UI 集合；通过自定义窗口消息或调度器把不可变结果投递到 UI 线程。
- 线程池定时器回调可能并发或与关闭竞态。状态必须串行化，并在析构前取消定时器、等待回调完成。
- 少量有明确所有权的长任务可用 `std::jthread`；不引入通用并发框架。
- 图标加载、拖放解析和配置/条目保存线程在入口处兜住异常；单个任务或完成回调失败不得终止进程，也不得阻止后续请求处理。
- UI Automation Provider 的快照、动作回调、HWND 与连接状态属于跨线程共享状态；读取时复制所需回调并在锁外调用，断开后不得再触达窗口回调。

## 生命周期

- Application 层拥有 COM/OLE 初始化、服务装配与反向销毁顺序。
- D2D/DWrite/WIC factory 可为进程级；窗口设备资源跟随 HWND/设备生命周期。
- 每个可跨显示器的顶层 HWND 独立使用 `GetDpiForWindow`/`WM_DPICHANGED` 管理几何与设备资源，不能沿用所有者窗口或进程启动时的 DPI。
- 渲染器必须处理 device lost，区分 device-independent 与 device-dependent 资源。
- Named Mutex 必须由主实例在整个进程生命周期持有；IPC 只负责激活，不承担所有权锁。
- Windows 注销或关机通过 `WM_QUERYENDSESSION`/`WM_ENDSESSION` 进入受控退出路径，在消息循环结束前刷新条目和配置保存队列；普通交互期间仍禁止在 UI 线程执行持久化 I/O。

## 建议目录

```text
src/{app,core,ui,graphics,activation,platform/windows,infrastructure}/
resources/{branding,windows}/
tests/
docs/ai/
```

实际目录出现后，以保持上述职责边界为目标，不要求机械复制目录树。
