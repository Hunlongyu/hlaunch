# 可访问性契约

当前实现状态（2026-08-26）：Launcher 已具备 Grid/Tab 与跨分类搜索键盘路径、方向键跨页、PageUp/PageDown、Enter 启动、`Insert` 添加、`F2` 编辑、Esc 清空/关闭和非纯颜色的条目焦点轮廓，并完成真实窗口消息验证。条目编辑器使用原生 Win32 Controls，可通过 Tab 顺序和默认按钮操作。Launcher 的 UI Automation Provider、Narrator、Accessibility Insights、高对比度与文本缩放仍未实现或验证，因此 `UIA-001` 仅为部分验证。

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

- 颜色不是唯一状态信号；键盘焦点始终有可见轮廓。
- 高对比度开启时使用系统颜色，并停用妨碍可读性的透明和背景图片。
- 尊重系统文本缩放、DPI 和减少动画设置；动画不得阻塞命中测试或 UIA 树更新。
- 为搜索、Tab、Grid 导航、启动、编辑、删除、设置和关闭定义稳定键盘路径。
- 不依赖 hover 才显示完成任务所需的信息。

## 发布验证

至少完成键盘全流程、Narrator、Accessibility Insights 自动检查与手工检查、高对比度、200% DPI、文本缩放和减少动画验证。问题记录必须包含 Windows build、DPI、主题、输入方式及可重复步骤。
