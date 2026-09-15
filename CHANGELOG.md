# 更新日志 / Changelog

## [Unreleased]

### 中文

- 拖入快捷方式后可删除原文件，保留启动参数、工作目录、图标和特殊启动方式。
- 开机自启改用登录计划任务，避免 Explorer 重启导致漏启动，并自动迁移旧自启设置。
- 登录自启保持普通权限和托盘驻留，重复启动不会打断正在使用的窗口。
- 登录自启不额外等待，以正常优先级运行，桌面未就绪时自动补建托盘图标。

### English

- Keep imported shortcuts working after the original file is deleted, preserving arguments, working directory, icons, and special launch behavior.
- Use a logon task for startup to avoid missed launches when Explorer restarts, and migrate existing startup settings.
- Keep logon startup at normal privileges in the tray, without disturbing an already running instance.
- Start at logon without an added delay, use normal process priority, and retry the tray icon when the desktop is not ready.

## [0.1.20] - 2026-09-08

### 中文

- 首次公开发布，支持启动应用、文件、文件夹、快捷方式和网址。
- 通过图标网格与分类整理项目，支持拖放添加、排序和跨分类搜索。
- 支持全局快捷键、屏幕边缘唤起、窗口置顶与可调透明度。
- 提供 x64、x86、ARM64 便携程序和 SHA-256 校验文件。
- 更新中英文工具介绍，加入界面截图与快捷键说明。

### English

- First public release, with support for launching apps, files, folders, shortcuts, and websites.
- Organize items in an icon grid with tabs, drag-and-drop, reordering, and cross-tab search.
- Open the launcher with a global hotkey or screen-edge activation; pin the window and adjust its opacity.
- Provide portable x64, x86, and ARM64 executables with SHA-256 checksums.
- Refresh the Chinese and English introductions with screenshots and keyboard shortcuts.

## [0.1.19] - 2026-08-29

### 中文

- 修复删除后重新添加同路径程序时，图标仍显示旧内容的问题。

### English

- Fix stale icons when a program is removed and then added again from the same path.
