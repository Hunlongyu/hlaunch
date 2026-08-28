# HLaunch

HLaunch 是一款面向 Windows 11 x64 的轻量级 Grid 快速启动器，并尽力兼容 Windows 10 x64（22H2 ESU 或仍受支持的 LTSC）。它以可视化图标网格为主、搜索为辅，支持快捷键和屏幕边缘停留两种唤起方式。

## 计划中的主要功能

- Grid 与 Tab 分类
- 应用、文件、文件夹、网址和快捷方式启动
- 全局快捷键与屏幕边缘停留唤起
- 拖放添加、排序、条目编辑和删除
- 跨分类搜索、托盘、开机启动与便携模式
- 多显示器和 Per-Monitor DPI V2

## 技术方向

项目使用 C++23、CMake、Win32、Direct2D、DirectWrite 和 WIC，发布目标是无需额外运行库的单个 `HLaunch.exe`。配置和缓存保存在 EXE 外部；“单 EXE”仅表示程序运行不需要随附框架 DLL。

## 当前状态

当前已经生成可运行的原生 `HLaunch.exe`：包含 Core 数据模型、独立 Glaze 适配层、便携优先数据目录、原子写入与 `.bak` 恢复、诊断日志、用户隔离的单实例应用壳、全局快捷键、默认关闭的屏幕边缘停留唤起和托盘入口，以及无边框竖向 Direct2D Launcher。托盘悬浮提示第一行显示名称、第二行显示当前版本。主窗口默认采用接近 CLaunch 的 394 × 594 DIP、5 × 8 槽位布局，Grid 与 Tab 间距为 8 DIP，16 DIP 紧凑标题区及左右按钮统一使用系统字体图标和弱化文字色；标题、空槽、条目和 Tab 分别提供区域菜单。关闭按钮只收起窗口；运行期置顶会把主窗口设为 Topmost，并阻止失焦自动收起。窗口默认使用 Acrylic，并支持 Solid、Mica、Acrylic、Tabbed 系统背景、半透明界面表面与 30%–100% 整体透明度；默认隐藏的搜索框使用与主窗口等宽的底部附属窗，出现时不改变 Grid 布局。除 Grid、Tab 和标题交互按钮外的主客户区可拖动窗口。Tab 和 Grid 已读取 `items.json`，支持分类新增、重命名、删除、拖拽排序和页面批量启动，也支持方向键、Home/End、PageUp/PageDown、Tab/Shift+Tab、Enter 和 Esc 键盘操作。Launcher 显示、搜索更新或切换分类后默认没有选中项；首次按导航键才显示条目焦点轮廓，之后 `Enter`、`F2` 和 `Delete` 才作用于该条目。鼠标滚轮在主界面任意区域切换分类，超出单屏容量的条目使用 `PageUp`/`PageDown` 浏览并保持键盘焦点可见。直接输入字符或按 `Ctrl+F` 可打开跨分类搜索，全部结果按匹配质量与已持久化的使用统计稳定排序并支持键盘分页。双击 Grid 不执行操作；使用空槽菜单或按 `Insert` 可打开统一的原生项目编辑器，按 `F2` 可编辑当前条目。右键菜单已接入启动、管理员启动、打开位置、复制、插入、移动分类、删除和属性；`Delete` 可删除焦点条目，删除确认默认选择“否”。设置和项目编辑器使用 Windows 默认浅色原生外观，主窗与搜索窗使用固定内置深色视觉。条目可在当前 Grid 内拖拽排序，也可精确拖到其他 Tab 与 Grid 槽位；目标位置会高亮并随其他修改一起持久化。文件、文件夹、`.exe`、`.lnk` 与 URL 可直接拖入当前分类，批量顺序会保留，完全重复时默认跳过。程序、文件、文件夹和快捷方式图标由 Windows Shell 在后台提取并通过 WIC/Direct2D 显示；提取失败时回退到首字符占位。修改会立即刷新 Grid、搜索索引与 UI Automation 树，并在后台串行、原子地写入数据文件。

数据目录默认创建在 `HLaunch.exe` 同目录的 `data`；启动时会验证目录及已有配置文件可读写，无法使用时自动回退 `%LOCALAPPDATA%\HLaunch`。

设置窗口使用“常规 / 唤起”双页签和统一的“确定 / 取消 / 应用”操作区；支持 30%–100% 主窗口透明度、全局快捷键重绑、八方向边缘热区、多显示器模式、边缘宽度、角落大小、停留/采样/冷却时间、全屏抑制、当前用户开机启动和诊断日志启停。主窗口使用固定内置深色视觉；材质与 Grid 行列保留为配置及诊断能力，不提供主题管理。EXE Manifest 已启用 Common Controls v6，提示与确认优先使用 `TaskDialogIndirect`，缺少 v6 能力时安全回退。自绘窗口和原生编辑窗统一读取 Windows 消息字体，并在系统颜色、字体、高对比度、DWM 合成或颜色变化时即时刷新；高对比度会使用系统色并临时关闭透明材质。进程使用稳定的 `Hunlongyu.HLaunch` AppUserModelID。

发生启动或运行问题时，优先在 `HLaunch.exe` 同目录下的 `data\logs` 查看最新日志；若软件目录没有读写权限，则查看 `%LOCALAPPDATA%\HLaunch\logs`。日志不记录条目名称、目标和参数。每个日志最多 2 MiB，最多保留 5 个，并自动清理超过 7 天的文件。

## 准备构建环境

在 MSVC x64 开发者终端中运行：

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

CMake 会通过 FetchContent 获取固定 revision 的 WIL、Glaze 和测试专用 doctest。

面向 AI 和开发者的详细约束、模块边界及待决事项见 [`docs/ai/README.md`](docs/ai/README.md)。
