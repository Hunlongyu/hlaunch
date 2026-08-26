# 工程与技术栈

## 基线

| 类别 | 选择 |
| --- | --- |
| 语言 | C++23 |
| 构建 | CMake 3.28+，Ninja，MSVC |
| 窗口 | Classic Win32 |
| COM/WinRT | C++/WinRT，`winrt::com_ptr` |
| Win32 RAII | WIL |
| 图形 | Direct2D、DirectWrite、WIC、DWM |
| 数据 | JSON + Glaze |
| 测试 | doctest，仅测试目标 |
| 发布 | x64 Release，静态 CRT `/MT` |

“单 EXE”表示不附带 VC++ Redistributable、Qt、.NET、WebView2 或第三方运行时 DLL；配置、日志、缓存和用户主题是正常外部数据。

## 构建规则

- `CMakeLists.txt` 是唯一权威构建定义，生成的 `.sln` 不是。
- 使用 target-based CMake，不使用全局 `include_directories()` 或 `add_definitions()`。
- 建议目标：`HLaunch::Core`、`HLaunch::Graphics`、`HLaunch::Platform`、`HLaunch::UI` 和最终 `HLaunch`。
- 用 `CMAKE_CXX_STANDARD 23` 表达标准，不在文档中把 `/std:c++latest` 当成稳定 ABI/语言契约。
- MSVC 建议启用 `/permissive- /utf-8 /W4 /EHsc /MP`；Release 可用 `/O2 /GL /LTCG /OPT:REF /OPT:ICF`，以编译器支持和测量结果为准。
- 用 `CMAKE_MSVC_RUNTIME_LIBRARY` 设置 Release `/MT`、Debug `/MTd`。
- 构建时先通过 `vswhere.exe -prerelease` 或 VS 开发者环境发现 MSVC，不硬编码版本目录。
- 开发基线使用 Windows SDK `10.0.26100.0` 或更新的兼容 SDK。C++/WinRT 头文件取自所选 Windows SDK，不额外引入 NuGet 包；调用可能缺失于兼容系统的 API 时必须先做运行时能力检测。

## Unicode 与错误

- Win32 API 统一调用 `W` 版本；Windows 边界用 UTF-16，JSON 文件用 UTF-8。
- 转换集中在 Infrastructure/Platform，禁止散落 ANSI code page 转换。
- 普通可恢复错误使用 `std::expected<T, Error>` 或等价项目类型；异常只用于初始化失败等无法在当前层恢复的情况。
- 错误对象保留操作、系统错误码和用户安全消息，日志中可记录诊断上下文。

## 依赖

产品第三方开源依赖仅包含 WIL 和 Glaze；doctest 仅进入测试目标。三者均为 MIT License。可读的上游版本写在 `cmake/DependencyVersions.cmake` 注释中，完整 commit hash 是唯一版本真相；FetchContent 不跟踪分支、浮动标签或 `latest`。

依赖声明集中在 `cmake/Dependencies.cmake`：

- `WIL::WIL`：项目创建的 header-only interface target。WIL 上游发布版要求 CMake 3.29 且默认启用自身测试和打包，因此 FetchContent 只下载其源码，不加入上游子目录。
- `glaze::glaze`：上游 C++23 header-only target。关闭 install、examples、EETF、SSL 和 C++26 reflection；配置读写采用 size 优化并关闭强制内联，以降低编译时间和二进制体积。
- `doctest::doctest`：仅在 `BUILD_TESTING=ON` 时获取。
- `HLaunch::Dependencies`：产品 target 使用的统一依赖入口，包含 WIL 和 Glaze，不包含 doctest。

Glaze 仅能出现在 `infrastructure/json` 适配层。持久化 DTO 与领域对象分离，字段名通过专用 DTO 或显式 metadata 固定，避免 C++ 成员重命名意外改变磁盘格式。解析选项和错误转换集中管理；为兼容同 schema 的扩展字段，必须显式设置 `error_on_unknown_keys=false`，不能依赖 Glaze 默认值。配置语料覆盖字段乱序、缺失、未知字段、坏 UTF-8、损坏输入和迁移。

新增依赖需要说明：Windows SDK 是否已有能力、自己实现的风险、维护成本、静态链接能力、运行时影响、许可证和二进制体积。依赖必须由适配层隔离。

## 窗口效果

- Platform 层封装 DWM 背景材质、边框颜色、圆角和窗口整体透明度，UI 层只选择效果并渲染透明表面。
- 使用 Windows SDK 的 `DWMWA_SYSTEMBACKDROP_TYPE`、`DwmExtendFrameIntoClientArea` 和 `DwmEnableBlurBehindWindow`；不使用未公开的 `SetWindowCompositionAttribute`。
- 请求的材质不可用时按 DWM 系统背景 > 系统模糊 > 半透明主题表面的顺序降级，任何一级失败都不能阻止窗口显示。
- Direct2D 透明窗口使用 BGRA premultiplied alpha；文字与关键状态色保持完整 alpha。

## 明确不引入

V1 不使用 Qt、WinUI 3、Windows App SDK、WTL、ATL、WRL、Boost、TBB、libuv、SQLite、libcurl、OpenSSL、WebView2、.NET 或脚本运行时。WTL 不是永久禁止；若未来设置页大量采用原生控件，可作为新决策重新评估。

## 系统库

预计链接 user32、shell32、ole32、advapi32、d2d1、dwrite、windowscodecs、dwmapi、shcore、shlwapi 和 comctl32。只有实际使用时才加入库；`shcore` 当前用于按显示器取得有效 DPI。P1 在线更新和崩溃转储分别需要 winhttp 与 dbghelp。
