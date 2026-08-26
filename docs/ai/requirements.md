# V1 需求追踪

本表给稳定需求 ID 和最低验收证据。`计划` 表示只有文档；`部分验证` 表示已有可重复证据但尚未达到该行的最低验收证据；`已验证` 必须附满足最低要求的可重复证据。实现时测试名称应包含对应 ID，不能仅凭代码存在改状态。

| ID | 需求 | 规范 | 最低证据 | 当前状态 |
| --- | --- | --- | --- | --- |
| PROD-GRID-001 | Grid 优先、Tab 分类并支持跨 Tab 搜索 | `product-scope.md`、`launcher-ui.md` | UI 集成 + 键盘运行验证 | 部分验证（2026-08-26：`items.json` Tab/Grid 绑定、鼠标分类切换、点击启动、空状态、底部附属搜索窗、DIP 命中、方向/Home/End、Tab 回绕、Enter 启动和 Esc 隐藏测试通过；搜索和滚动交互未实现） |
| UI-EFFECT-001 | 无边框窗口支持系统材质、表面 alpha 与整体透明度，并可安全降级 | `launcher-ui.md`、`themes.md` | 参数单元测试 + 各材质 Windows 运行截图 + 高对比度验证 | 部分验证（2026-08-26：参数测试、四种材质窗口创建与 DWM 属性读取通过；材质截图和高对比度未验证） |
| UI-DRAG-001 | 除搜索、Grid、Tab 和关闭按钮外的客户区可拖动无边框窗口 | `launcher-ui.md` | 命中测试单元测试 + Windows 拖拽运行验证 | 部分验证（2026-08-26：DIP 单元测试与真实窗口 7 点 `WM_NCHITTEST` 探测通过；实际鼠标移动结果待人工确认） |
| ACT-HOTKEY-001 | 默认启用 `Alt+Space`，冲突可恢复 | `activation-hotkey.md` | 单元 + Windows 运行验证 | 部分验证（2026-08-26：结构化映射与冲突单元测试、真实 `RegisterHotKey`/`SendInput`/`WM_HOTKEY` 集成测试、Debug 默认隐藏与 Toggle 居中运行验证；设置页与物理键盘矩阵未完成） |
| ACT-EDGE-001 | 边缘默认关闭，启用后 Show only | `activation-edge.md` | 状态机单元 + 多显示器运行验证 | 部分验证（2026-08-26：热区/状态机/多屏几何单元测试、线程池左外边缘 Show-only 与重武装 Debug 运行验证；完整多屏、全屏应用与性能矩阵未完成） |
| DATA-CONFIG-001 | schema v1、校验、迁移与原子写入 | `data-and-config.md`、`schemas/` | 配置语料 + 故障注入集成测试 | 部分验证（2026-08-26：memory-codec + storage 故障保护；迁移与异步保存未完成） |
| PLAT-SINGLE-001 | Mutex 所有权与隐藏窗口激活 | `windows-integration.md` | 双实例 + 完整性级别运行验证 | 部分验证（2026-08-26：同级双实例 hide/show 运行通过；UIPI 矩阵未验证） |
| PLAT-TRAY-001 | 托盘显示/隐藏、设置、退出与 Explorer 恢复 | `windows-integration.md` | 生命周期集成测试 + Explorer 运行验证 | 部分验证（2026-08-26：真实图标矩形、选择回调 Toggle 和生命周期测试通过；设置页、菜单人工操作与 Explorer 重启验证未完成） |
| PLAT-SHELL-001 | 目标、逻辑参数与工作目录分离并通过 Shell 启动 | `windows-integration.md`、`data-and-config.md` | 参数编解码集成测试 + Windows 运行验证 | 部分验证（2026-08-26：引号边界、无效 UTF-8、真实 `ShellExecuteExW` 参数回读及两 Tab 点击启动通过；文件/文件夹/URL、UAC 取消和失效目标矩阵未完成） |
| UIA-001 | 键盘和 UI Automation 可操作 | `accessibility.md` | Narrator + Accessibility Insights | 部分验证（2026-08-26：Grid/Tab 键盘路径、Enter/Esc 与可见焦点已实现并完成真实窗口消息验证；UIA Provider、Narrator、Accessibility Insights 和高对比度未验证） |
| QUALITY-LOG-001 | 诊断日志受容量、数量、时间和隐私边界约束，并记录关键生命周期及未处理异常 | `quality.md` | 单元测试 + Windows 启动/退出日志验证 | 已验证（2026-08-26：9 个 CTest 全通过；覆盖净化、容量、数量、过期和异常记录，Debug 便携运行日志覆盖数据加载、窗口/服务创建与正常退出；配置关闭开关待后续实现） |
| BUILD-DEPS-001 | 依赖固定 commit 且只进入允许目标 | `engineering.md` | MSVC Configure/Build/CTest + cache 检查 | 已验证（2026-08-20 Debug） |
| RELEASE-001 | x64 `/MT` 单 EXE、无第三方运行时 DLL | `engineering.md`、`quality.md` | 干净 Release 构建 + 二进制依赖检查 | 部分验证（2026-08-26：Release 编译 + 仅系统 DLL；干净发布演练未完成） |

状态变化时在同一行补日期和证据路径。文档验收条件、自动测试与运行记录应引用同一个 ID；一个 smoke test 不能替代产品功能证据。
