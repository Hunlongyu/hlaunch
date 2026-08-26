# AI 文档索引

这些文档是 HLaunch 的设计源，面向 AI 和开发者。它们采用短章节、明确约束和状态标记，避免从长篇叙述中猜测需求。

## 状态词

- **必须**：V1 约束，变更前需要明确决策。
- **建议**：默认方案，可在有证据时调整。
- **待决**：尚未确认，不得当作已实现需求。
- **未来**：不属于 V1。

## 按任务读取

| 任务 | 必读文档 |
| --- | --- |
| 判断范围、优先级或默认行为 | `product-scope.md` |
| 新建模块、调整依赖方向或线程 | `architecture.md` |
| 快捷键 | `activation-hotkey.md` |
| 屏幕边缘、多显示器、全屏检测 | `activation-edge.md` |
| Grid、Tab、搜索和交互 | `launcher-ui.md` |
| 配置、条目、便携模式和迁移 | `data-and-config.md` |
| 主题、图片和渲染资源 | `themes.md` |
| Shell、拖放、托盘、单实例和启动 | `windows-integration.md` |
| 编译器、CMake、依赖和编码规范 | `engineering.md` |
| 测试、性能、日志、崩溃和安全 | `quality.md` |
| UI Automation、键盘和高对比度 | `accessibility.md` |
| 需求编号、验收条件和证据 | `requirements.md` |
| 已确认且需要保留原因的选择 | `decisions/README.md` |
| 尚未确认的产品或技术选择 | `open-decisions.md` |

## 事实边界

当前已有 CMake/FetchContent 工程基线、数据/config 模型与 Glaze 编解码、标准/便携目录、原子写入与 `.bak` 回退、单实例应用壳，以及可运行的无边框竖向 Direct2D 界面骨架。界面中的搜索、Tab、Grid 和条目均为静态呈现，不能据此推导搜索、导航、启动、拖放、快捷键、边缘唤起或 UI Automation 已实现。后续仍须分别记录代码、测试、构建和运行证据。

需求真相的优先级为：用户最新明确决定或已接受 ADR > 本目录规范文档 > 历史讨论。实现证据的可信度为：运行/集成验证 > 自动测试 > 编译 > 静态代码。实现与需求不符是缺陷，已通过测试的旧行为不能覆盖新的产品决定。发现冲突后应同时修正文档、测试和实现，不长期保留两套说法。
