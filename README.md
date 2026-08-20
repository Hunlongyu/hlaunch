# HLaunch

HLaunch 是一款面向 Windows 11 x64 的轻量级 Grid 快速启动器，并尽力兼容 Windows 10 x64（22H2 ESU 或仍受支持的 LTSC）。它以可视化图标网格为主、搜索为辅，计划支持快捷键和屏幕边缘停留两种唤起方式。

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

当前已经建立 CMake、FetchContent 依赖管理和依赖 smoke test，但尚无产品功能源码或 `HLaunch.exe`。因此功能描述仍是设计目标，不代表已经实现或运行验证。

## 准备构建环境

在 MSVC x64 开发者终端中运行：

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

CMake 会通过 FetchContent 获取固定 revision 的 WIL、Glaze 和测试专用 doctest。

面向 AI 和开发者的详细约束、模块边界及待决事项见 [`docs/ai/README.md`](docs/ai/README.md)。
