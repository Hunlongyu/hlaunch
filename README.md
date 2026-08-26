# HLaunch

HLaunch 是一款面向 Windows 11 x64 的轻量级 Grid 快速启动器，并尽力兼容 Windows 10 x64（22H2 ESU 或仍受支持的 LTSC）。它以可视化图标网格为主、搜索为辅，支持快捷键和屏幕边缘停留两种唤起方式。

## 计划中的主要功能

- Grid 与 Tab 分类
- 应用、文件、文件夹、网址和快捷方式启动
- 全局快捷键与屏幕边缘停留唤起
- 拖放添加、排序和条目编辑
- 搜索、主题、托盘、开机启动与便携模式
- 多显示器和 Per-Monitor DPI V2

## 技术方向

项目计划使用 C++23、CMake、Win32、Direct2D、DirectWrite 和 WIC，发布目标是无需额外运行库的单个 `HLaunch.exe`。配置、缓存和自定义主题仍会保存在 EXE 外部；“单 EXE”仅表示程序运行不需要随附框架 DLL。

## 当前状态

当前已经生成可运行的原生 `HLaunch.exe`：包含 Core 数据模型、独立 Glaze 适配层、标准/便携数据目录、原子写入与 `.bak` 恢复、用户隔离的单实例应用壳、全局快捷键、默认关闭的屏幕边缘停留唤起和托盘入口，以及无边框竖向 Direct2D Launcher。窗口默认使用 Acrylic，并支持 Solid、Mica、Acrylic、Tabbed 系统背景、半透明界面表面与 30%–100% 整体透明度；默认隐藏的搜索框使用跟随主窗口的底部附属窗，出现时不改变 Grid 布局。除 Grid、Tab 和关闭按钮外的主客户区可拖动窗口。Tab 和 Grid 已读取 `items.json`，鼠标可切换分类并通过 Windows Shell 启动当前条目。搜索输入、键盘导航、滚动/分页、添加与编辑、拖放、真实图标、设置和 UI Automation 仍是后续工作。

## 准备构建环境

在 MSVC x64 开发者终端中运行：

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

CMake 会通过 FetchContent 获取固定 revision 的 WIL、Glaze 和测试专用 doctest。

面向 AI 和开发者的详细约束、模块边界及待决事项见 [`docs/ai/README.md`](docs/ai/README.md)。
