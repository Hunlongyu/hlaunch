# 可访问性契约

当前实现状态（2026-08-27）：Launcher 已具备 Grid/Tab 与跨分类搜索键盘路径。默认没有条目选中，首次按导航键后才显示非纯颜色的焦点轮廓，并允许 Enter 启动、`F2` 编辑或 `Delete` 删除；PageUp/PageDown、`Insert` 添加和 Esc 清空/关闭也已接入。删除确认使用 `TaskDialogIndirect` 且默认“否”，取消不改变数据，成功后焦点保持在相邻条目。条目编辑器使用默认浅色原生 Win32 Controls，可通过 Tab 顺序和默认按钮操作。所有现有窗口统一使用系统消息字体，并响应系统外观广播；Launcher 在高对比度运行时使用系统色、关闭透明材质并恢复完整不透明度。Launcher 的 UI Automation Provider 已实现 Window、Tab/TabItem、List/ListItem 与标题按钮语义，并覆盖 Invoke、Selection、SelectionItem、稳定 Runtime ID 和状态事件；原生弹框高对比度人工检查、Narrator、Accessibility Insights 与文本缩放仍未完成验证，因此 `UIA-001` 仍为部分验证。

## 基线

所有主要操作必须能只用键盘完成。Direct2D 自绘不免除 UI Automation（UIA）责任；Settings 优先使用原生 Win32 Controls，Launcher 自绘节点提供自定义 UIA Provider。

## UIA 映射

| UI | Control Type | 必要 Pattern / 属性 |
| --- | --- | --- |
| Launcher | Window | Name、IsKeyboardFocusable |
| 搜索框 | Edit | Value、Text、可见焦点 |
| Tab 容器 / Tab | Tab / TabItem | Selection、SelectionItem、名称和选中状态 |
| 启动项列表 / 条目 | List / ListItem | Selection、SelectionItem、Invoke、名称、失效状态、位置 |
| 状态消息 | StatusBar 或 Text | Name；重要错误触发通知事件 |
| 设置按钮与菜单项 | Button / MenuItem | Invoke、Name、Enabled |

条目异步加载图标、重新排序或筛选时，保持逻辑焦点和稳定 Runtime ID；只对真实变化发 UIA 事件，避免 Narrator 重复朗读。

## 视觉与输入

- 颜色不是唯一状态信号；进入键盘选中状态后，焦点始终有可见轮廓。未选中状态不能暗示首项会响应 Enter、F2 或 Delete。
- 高对比度开启时使用系统颜色，并停用妨碍可读性的透明效果。
- 尊重系统文本缩放、DPI 和减少动画设置；动画不得阻塞命中测试或 UIA 树更新。
- 为搜索、Tab、Grid 导航、启动、编辑、删除、设置和关闭定义稳定键盘路径。
- 不依赖 hover 才显示完成任务所需的信息。

## 发布验证

至少完成键盘全流程、Narrator、Accessibility Insights 自动检查与手工检查、高对比度、200% DPI、文本缩放和减少动画验证。问题记录必须包含 Windows build、DPI、系统外观、输入方式及可重复步骤。

2026-08-27 修复补充：搜索输入已改为无边框原生 `EDIT`，由系统提供 IME、文本选择与 Text/Value UIA 语义。Launcher Grid/Tab 的自定义 UIA Fragment Provider 已实现，并通过真实 UI Automation 客户端自动测试验证条目 Invoke 与 Tab SelectionItem；Narrator 和 Accessibility Insights 人工验证尚未完成，`UIA-001` 仍为部分验证。
