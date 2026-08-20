# V1 需求追踪

本表给稳定需求 ID 和最低验收证据。`计划` 表示只有文档；`已验证` 必须附可重复证据。实现时测试名称应包含对应 ID，不能仅凭代码存在改状态。

| ID | 需求 | 规范 | 最低证据 | 当前状态 |
| --- | --- | --- | --- | --- |
| PROD-GRID-001 | Grid 优先、Tab 分类并支持跨 Tab 搜索 | `product-scope.md`、`launcher-ui.md` | UI 集成 + 键盘运行验证 | 计划 |
| ACT-HOTKEY-001 | 默认启用 `Alt+Space`，冲突可恢复 | `activation-hotkey.md` | 单元 + Windows 运行验证 | 计划 |
| ACT-EDGE-001 | 边缘默认关闭，启用后 Show only | `activation-edge.md` | 状态机单元 + 多显示器运行验证 | 计划 |
| DATA-CONFIG-001 | schema v1、校验、迁移与原子写入 | `data-and-config.md`、`schemas/` | 配置语料 + 故障注入集成测试 | 计划 |
| PLAT-SINGLE-001 | Mutex 所有权与隐藏窗口激活 | `windows-integration.md` | 双实例 + 完整性级别运行验证 | 计划 |
| UIA-001 | 键盘和 UI Automation 可操作 | `accessibility.md` | Narrator + Accessibility Insights | 计划 |
| BUILD-DEPS-001 | 依赖固定 commit 且只进入允许目标 | `engineering.md` | MSVC Configure/Build/CTest + cache 检查 | 已验证（2026-08-20 Debug） |
| RELEASE-001 | x64 `/MT` 单 EXE、无第三方运行时 DLL | `engineering.md`、`quality.md` | 干净 Release 构建 + 二进制依赖检查 | 计划 |

状态变化时在同一行补日期和证据路径。文档验收条件、自动测试与运行记录应引用同一个 ID；一个 smoke test 不能替代产品功能证据。
