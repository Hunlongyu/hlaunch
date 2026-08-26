# 数据、配置与持久化

## 文件与格式

- `config.json`：应用设置、激活策略、窗口和行为偏好。
- `items.json`：Tab、条目、顺序和使用统计。
- `theme.json`：单个主题的视觉令牌，见 `themes.md`。
- `logs/`：诊断日志目录，不属于 JSON schema，也不随配置备份。

V1 设计契约使用 UTF-8 JSON，根对象包含整数 `schemaVersion: 1`。这已经是实现输入，不再称为草稿，但在首个公开版本发布前仍可通过 ADR 变更；当前项目尚无可迁移的已发布用户数据。领域层不依赖 Glaze；JSON 读写和持久化 DTO 只存在于基础设施适配层。

当前实现状态（2026-08-26）：已建立独立 Core 模型和 Glaze DTO 适配层，支持 `config.json`、`items.json` 的内存编解码、字段/范围校验、高版本只读保护、输入规模限制和失效 item 隔离；已实现标准/便携数据目录、同目录临时文件、刷盘关闭、原子替换、单一 `.bak` 和损坏主文件回退。未知字段尚未逐字段生成诊断，异步保存合并、恢复确认 UI 和实际 schema 迁移仍未实现，因此 `DATA-CONFIG-001` 仍是部分验证。

## 标识、时间与条目

- Tab 和条目 ID 是小写 UUID v4 字符串，创建后稳定不变。
- 时间戳为 UTC RFC 3339 字符串，例如 `2026-08-20T09:30:00Z`；没有时间时使用 JSON `null`。
- `type` 只允许 `application`、`file`、`folder`、`url`、`shortcut`。
- `arguments` 是字符串数组，每项表示一个逻辑参数；不得持久化为待 Shell 再解析的整行命令。
- `workingDirectory` 和 `icon` 是字符串或 `null`。
- `launchCount` 是非负 64 位整数；`lastLaunchedAt` 是时间戳或 `null`。

```json
{
  "schemaVersion": 1,
  "tabs": [{
    "id": "72976493-b090-4ca3-bbb8-d1d97d9f4eaf",
    "name": "常用",
    "items": [{
      "id": "fb462c0f-3050-4ddd-a1bc-fbb66008fd9e",
      "type": "application",
      "name": "示例",
      "target": "C:\\Path\\App.exe",
      "arguments": ["--profile", "work"],
      "workingDirectory": null,
      "icon": null,
      "runAsAdministrator": false,
      "launchCount": 0,
      "lastLaunchedAt": null
    }]
  }]
}
```

## 数据根与便携模式

- 标准模式把可变数据放在 `%LOCALAPPDATA%\HLaunch`。
- EXE 同目录存在 `portable.flag` 时，便携模式把可变数据放在 `data` 子目录。
- 日志随数据根定位到 `logs` 子目录：标准模式为 `%LOCALAPPDATA%\HLaunch\logs`，便携模式为 `data\logs`。
- `--portable` 只对本次启动强制便携模式，不创建或删除标记文件。优先级为命令行 > 标记文件 > 标准模式。
- 相对路径只相对于数据根或 EXE 目录解析，禁止依赖进程当前工作目录。
- 只有位于便携根目录内的目标才能转为相对路径；跨盘或根目录外目标保持绝对路径。
- 环境变量展开、参数引用和快捷方式解析集中在 Windows 平台层。

## 读取、兼容与校验

- 配置解析与 key 顺序无关；缺失可选字段使用文档默认值。Glaze 读取选项显式关闭未知 key 报错，并保留 UTF-8 校验。
- 同一受支持 schema 内的未知字段可忽略并记录诊断，但保存时不保证保留。高于当前支持的 schema 必须只读保护并提示，禁止降级覆盖。
- 对枚举、数值范围、字符串长度、路径和数组规模做校验。解析安全上限为单文件 8 MiB、嵌套 32 层、128 个 Tab、总计 10000 个条目；这些是拒绝异常输入的上限，不是 UI 推荐容量。
- 单个失效条目应隔离并报告，不丢弃其余有效数据。
- 新版本先迁移内存模型，完整校验成功后再落盘。
- 正式字段和范围以本目录 `schemas/` 下的 JSON Schema 为准；示例不能替代 schema 与迁移测试。

## 安全写入

写入流程必须是“同目录临时文件 → 刷盘并关闭 → 原子替换”，同时只保留一个最近成功版本的 `.bak`。失败时保留原文件，不得用空对象覆盖。启动时若主文件损坏，应提供从备份恢复或导出诊断信息的路径。

多个异步保存请求需要串行化或合并。进程退出前等待必要的最终提交，但不在 UI 线程执行大文件 I/O。

## 配置示例

```json
{
  "schemaVersion": 1,
  "activation": {
    "hotkey": {
      "enabled": true,
      "modifiers": ["alt"],
      "key": "Space",
      "behavior": "toggle"
    },
    "screenEdge": {
      "enabled": false,
      "zones": ["left"],
      "edgeMode": "desktopOuter",
      "thicknessDip": 4,
      "cornerSizeDip": 16,
      "dwellMs": 300,
      "pollMs": 40,
      "cooldownMs": 500,
      "disableOnFullscreen": true
    }
  }
}
```
