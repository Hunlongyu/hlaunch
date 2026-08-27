# HLaunch

HLaunch 是一款面向 Windows 11 x64 的轻量级 Grid 快速启动器，并尽力兼容 Windows 10 x64（22H2 ESU 或仍受支持的 LTSC）。它以可视化图标网格为主、搜索为辅，支持快捷键和屏幕边缘停留两种唤起方式。

## 计划中的主要功能

- Grid 与 Tab 分类
- 应用、文件、文件夹、网址和快捷方式启动
- 全局快捷键与屏幕边缘停留唤起
- 拖放添加、排序、条目编辑和删除
- 跨分类搜索、主题、托盘、开机启动与便携模式
- 多显示器和 Per-Monitor DPI V2

## 技术方向

项目计划使用 C++23、CMake、Win32、Direct2D、DirectWrite 和 WIC，发布目标是无需额外运行库的单个 `HLaunch.exe`。配置、缓存和自定义主题仍会保存在 EXE 外部；“单 EXE”仅表示程序运行不需要随附框架 DLL。

## 当前状态

当前已经生成可运行的原生 `HLaunch.exe`：包含 Core 数据模型、独立 Glaze 适配层、标准/便携数据目录、原子写入与 `.bak` 恢复、诊断日志、用户隔离的单实例应用壳、全局快捷键、默认关闭的屏幕边缘停留唤起和托盘入口，以及无边框竖向 Direct2D Launcher。主窗口采用接近 CLaunch 的 394 × 605 DIP、5 × 8 固定槽位布局，标题、空槽、条目和 Tab 分别提供区域菜单；关闭按钮只收起窗口，失焦自动收起可由运行期窗口锁定阻止。窗口默认使用 Acrylic，并支持 Solid、Mica、Acrylic、Tabbed 系统背景、半透明界面表面与 30%–100% 整体透明度；默认隐藏的搜索框使用与主窗口等宽的底部附属窗，出现时不改变 Grid 布局。除 Grid、Tab 和标题交互按钮外的主客户区可拖动窗口。Tab 和 Grid 已读取 `items.json`，支持基本分类新增、重命名和删除，鼠标切换与点击启动，也支持方向键、Home/End、PageUp/PageDown、Tab/Shift+Tab、Enter 和 Esc 键盘操作。Launcher 显示、搜索更新或切换分类后默认没有选中项；首次按导航键才显示条目焦点轮廓，之后 `Enter`、`F2` 和 `Delete` 才作用于该条目。鼠标滚轮在主界面任意区域切换分类，超出单屏容量的条目使用 `PageUp`/`PageDown` 浏览并保持键盘焦点可见。直接输入字符或按 `Ctrl+F` 可打开跨分类搜索，全部结果按匹配质量与使用统计稳定排序并支持键盘分页。双击空槽、使用空槽菜单或按 `Insert` 可打开原生快速注册窗；按 `F2` 可打开当前条目的原生属性窗，右键菜单已接入插入、删除和属性，移动分类等后续命令先以禁用项保留；`Delete` 可删除焦点条目，删除确认默认选择“否”。设置、注册和属性对话框跟随 Windows 外观，HLaunch 深浅主题覆盖自绘主窗与搜索窗。条目可在当前 Grid 内拖拽排序，也可拖到其他 Tab 末尾；目标位置会高亮并随其他修改一起持久化。文件、文件夹、`.exe`、`.lnk` 与 URL 可直接拖入当前分类，批量顺序会保留，完全重复时默认跳过。程序、文件、文件夹和快捷方式图标由 Windows Shell 在后台提取并通过 WIC/Direct2D 显示；提取失败时回退到首字符占位。修改会立即刷新 Grid 与搜索索引，并在后台串行、原子地写入 `items.json`。分类排序、完整设置页和 UI Automation Provider 仍是后续工作。

设置页现已支持主题、Solid/Mica/Acrylic/Tabbed 背景材质、30%–100% 整体不透明度、全局快捷键重绑、边缘唤起启停和全屏抑制；保存后立即应用。完整设置页尚缺边缘高级参数和开机启动选项。

发生启动或运行问题时，可在 `%LOCALAPPDATA%\HLaunch\logs` 查看最新日志；便携模式对应 `HLaunch.exe` 同目录下的 `data\logs`。日志不记录条目名称、目标和参数。每个日志最多 2 MiB，最多保留 5 个，并自动清理超过 7 天的文件。

## 准备构建环境

在 MSVC x64 开发者终端中运行：

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

CMake 会通过 FetchContent 获取固定 revision 的 WIL、Glaze 和测试专用 doctest。

面向 AI 和开发者的详细约束、模块边界及待决事项见 [`docs/ai/README.md`](docs/ai/README.md)。
