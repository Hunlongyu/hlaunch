# V1 需求追踪

本表给稳定需求 ID 和最低验收证据。`计划` 表示只有文档；`部分验证` 表示已有可重复证据但尚未达到该行的最低验收证据；`已验证` 必须附满足最低要求的可重复证据。实现时测试名称应包含对应 ID，不能仅凭代码存在改状态。

| ID | 需求 | 规范 | 最低证据 | 当前状态 |
| --- | --- | --- | --- | --- |
| PROD-GRID-001 | Grid 优先、Tab 分类并支持跨 Tab 搜索 | `product-scope.md`、`launcher-ui.md` | UI 集成 + 键盘运行验证 | 已验证（2026-08-26：`items.json` Tab/Grid 绑定、394 × 605 DIP 的 5 × 8 固定槽位、区域菜单、基本分类新增/重命名/删除、默认无选中与导航键激活、鼠标滚轮全区域切换分类、鼠标/键盘启动、同 Tab 拖拽重排、跨 Tab 末尾移动、目标高亮、底部附属搜索窗、DIP 命中、可见焦点、位置指示，以及直接输入/Ctrl+F、Unicode 跨 Tab 搜索、稳定排序、退格/粘贴、方向/Home/End、PageUp/PageDown、Enter 和两阶段 Esc 已实现；Core、窗口级键盘/滚轮/拖拽与区域菜单测试以及 Debug 运行验证通过） |
| PROD-ITEM-001 | 添加、编辑、删除和跨 Tab 移动条目并安全持久化 | `launcher-ui.md`、`data-and-config.md` | Launcher 窗口集成 + Core 变更测试 + 后台存储测试 | 已验证（2026-08-26：空槽双击/菜单/`Insert` 原生快速注册、`F2`/右键原生属性、`Delete`/右键删除及鼠标重排已接入；Debug Launcher 窗口测试覆盖全部属性字段、逻辑参数拆分、同 Tab 最终位置重排、跨 Tab 移动、删除取消与确认、相邻焦点、稳定 UUID 和搜索索引刷新，Core/存储测试覆盖无效移动原子性、删除顺序、使用统计保留、首次默认分类、后台快照合并和退出刷新） |
| PROD-DROP-001 | 从文件系统和浏览器拖入添加条目，保留批量顺序并处理完全重复 | `launcher-ui.md`、`windows-integration.md` | OLE 数据对象集成 + 窗口集成 + Explorer/浏览器运行验证 | 部分验证（2026-08-26：`IDropTarget` 注册/撤销、`CF_HDROP`/URL 提取、后台类型解析、目标 Tab、批量顺序、完全重复默认跳过和后台持久化已实现；Core、OLE 数据对象、解析器与真实窗口注册测试通过，Explorer/浏览器鼠标矩阵待人工验证） |
| UI-EFFECT-001 | 无边框窗口支持系统材质、表面 alpha、整体透明度和一致主题，并可安全降级 | `launcher-ui.md`、`themes.md` | 参数单元测试 + 各材质/主题 Windows 运行截图 + 高对比度验证 | 部分验证（2026-08-26：默认 95% 整体不透明度及边界参数测试、四种材质窗口创建与 DWM 属性读取、非 Solid 客户区透明清除、Acrylic 合成截图、Launcher/搜索内置深浅主题语义令牌、原生设置即时切换主题/材质/透明度、schema v1 持久化与旧配置兼容测试通过；设置/注册/属性按设计跟随 Windows 而非自绘主题；Mica/Tabbed/Solid 完整主题截图矩阵、外部主题和高对比度未验证） |
| UI-ICON-001 | Grid 显示 Windows Shell 真实图标，异步加载失败时安全回退 | `launcher-ui.md`、`themes.md`、`windows-integration.md` | Shell/WIC 单元测试 + Launcher 运行验证 | 部分验证（2026-08-26：显式图标优先、目标 Shell 图标、后台串行提取、预乘 BGRA、DPI 相关内存缓存、设备丢失重建和首字符回退已实现；真实 EXE 图标与后台回调自动测试通过，Launcher 人工视觉矩阵待验证） |
| UI-DRAG-001 | 除搜索、Grid、Tab 和标题交互按钮外的客户区可拖动无边框窗口 | `launcher-ui.md` | 命中测试单元测试 + Windows 拖拽运行验证 | 部分验证（2026-08-26：DIP 单元测试覆盖 Grid、Tab、主菜单、锁定和关闭按钮，真实窗口 7 点 `WM_NCHITTEST` 探测通过；实际鼠标移动结果待人工确认） |
| ACT-HOTKEY-001 | 默认启用 `Alt+Space`，冲突可恢复 | `activation-hotkey.md` | 单元 + Windows 运行验证 | 部分验证（2026-08-27：结构化映射与冲突单元测试、真实 `RegisterHotKey`/`SendInput`/`WM_HOTKEY` 集成测试、Debug 默认隐藏与 Toggle 居中运行验证；设置页已支持启用、组合键重绑、运行时应用、冲突/保存失败回滚与持久化，物理键盘矩阵未完成） |
| ACT-EDGE-001 | 边缘默认关闭，启用后 Show only | `activation-edge.md` | 状态机单元 + 多显示器运行验证 | 部分验证（2026-08-27：热区/状态机/多屏几何单元测试、线程池左外边缘 Show-only 与重武装 Debug 运行验证；设置页已支持运行时启停和全屏抑制开关，边缘高级选项、完整多屏、全屏应用与性能矩阵未完成） |
| DATA-CONFIG-001 | schema v1、校验、迁移与原子写入 | `data-and-config.md`、`schemas/` | 配置语料 + 故障注入集成测试 | 部分验证（2026-08-26：memory-codec 覆盖主题、材质、透明度、旧版默认值和非法范围，storage 覆盖故障保护，条目使用后台串行/合并保存；通用配置异步保存和跨 schema 迁移未完成） |
| PLAT-SINGLE-001 | Mutex 所有权与隐藏窗口激活 | `windows-integration.md` | 双实例 + 完整性级别运行验证 | 部分验证（2026-08-26：同级双实例 hide/show 运行通过；UIPI 矩阵未验证） |
| PLAT-TRAY-001 | 托盘显示/隐藏、设置、退出与 Explorer 恢复 | `windows-integration.md` | 生命周期集成测试 + Explorer 运行验证 | 部分验证（2026-08-27：真实图标矩形、选择回调 Toggle、设置入口、可复用原生设置窗、外观与基础激活配置、标准对话框键盘路由和生命周期测试通过；开机启动设置、菜单人工操作与 Explorer 重启验证未完成） |
| PLAT-SHELL-001 | 目标、逻辑参数与工作目录分离并通过 Shell 启动 | `windows-integration.md`、`data-and-config.md` | 参数编解码集成测试 + Windows 运行验证 | 部分验证（2026-08-26：引号边界、无效 UTF-8、真实 `ShellExecuteExW` 参数回读及两 Tab 点击启动通过；文件/文件夹/URL、UAC 取消和失效目标矩阵未完成） |
| UIA-001 | 键盘和 UI Automation 可操作 | `accessibility.md` | Narrator + Accessibility Insights | 部分验证（2026-08-26：Grid/Tab/搜索键盘路径、默认无选中、导航键激活焦点、PageUp/PageDown、受选中状态约束的 Enter/`F2`/`Delete`、`Insert`、两阶段 Esc 与可见焦点已实现并完成真实窗口消息验证；UIA Provider、Narrator、Accessibility Insights 和高对比度未验证） |
| QUALITY-LOG-001 | 诊断日志受容量、数量、时间和隐私边界约束，并记录关键生命周期及未处理异常 | `quality.md` | 单元测试 + Windows 启动/退出日志验证 | 已验证（2026-08-26：13 个 CTest 覆盖相关回归；日志测试覆盖净化、容量、数量、过期和异常记录，Debug 便携运行日志覆盖数据加载、窗口/服务创建与正常退出；配置关闭开关待后续实现） |
| BUILD-DEPS-001 | 依赖固定 commit 且只进入允许目标 | `engineering.md` | MSVC Configure/Build/CTest + cache 检查 | 已验证（2026-08-20 Debug） |
| RELEASE-001 | x64 `/MT` 单 EXE、无第三方运行时 DLL | `engineering.md`、`quality.md` | 干净 Release 构建 + 二进制依赖检查 | 部分验证（2026-08-26：Release 编译 + 仅系统 DLL；干净发布演练未完成） |

状态变化时在同一行补日期和证据路径。文档验收条件、自动测试与运行记录应引用同一个 ID；一个 smoke test 不能替代产品功能证据。
