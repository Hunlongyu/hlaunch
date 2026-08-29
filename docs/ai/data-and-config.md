# 数据、配置与持久化

## 文件与格式

- `config.json`：应用设置、激活策略、窗口和行为偏好。
- `items.json`：Tab、条目、稳定 Grid 槽位和使用统计。
- `logs/`：诊断日志目录，不属于 JSON schema，也不随配置备份。
- `cache/icons/`：可丢弃的 Shell 图标像素缓存，不属于用户数据或备份契约；按条目 UUID 隔离，删除条目时后台清除对应缓存。

V1 设计契约使用 UTF-8 JSON，根对象包含整数 `schemaVersion: 1`。这已经是实现输入，不再称为草稿，但在首个公开版本发布前仍可通过 ADR 变更；当前项目尚无可迁移的已发布用户数据。领域层不依赖 Glaze；JSON 读写和持久化 DTO 只存在于基础设施适配层。

当前实现状态（2026-08-29）：已建立独立 Core 模型和 Glaze DTO 适配层，支持 `config.json`、`items.json` 的内存编解码、范围校验、输入规模限制与失效 item 隔离。`appearance` 只保存 DWM 材质、整体透明度和 Grid 行列；DWM 材质可在设置窗口选择 Solid、Mica、Acrylic 或 Tabbed，整体透明度可在同一区域以 30%–100% 整数修改，应用后更新主窗口与搜索窗的运行时效果并提交配置快照。`activation.screenEdge` 保存启用状态、热区、多显示器模式、全屏抑制和前台进程黑/白名单；五个边缘调节字段只为 schema v1 降级兼容而保留，读取时忽略历史值，运行时固定为 4 DIP、16 DIP、180 ms、30 ms、100 ms，保存时写回固定值。`diagnostics.loggingEnabled` 保存诊断日志开关，缺失时默认开启；主题、背景图、自定义字体和组件样式字段均已移除。条目和通用配置分别使用后台串行工作线程合并最新快照，再执行原子替换并保留单一 `.bak`；确认删除条目后提交的新 `items.json` 快照不再包含该条目，正常退出会等待最后一份待保存快照。激活设置先应用运行时服务再排队保存，最终保存失败时恢复上次持久化服务；Grid、材质或透明度保存失败时保留当前会话效果并明确提示未持久化。开机启动以当前用户 Run 注册表值为状态真相，不写入 `config.json`。

窗口 Grid 尺寸使用 `appearance.gridColumns` 与 `appearance.gridRows` 保存，缺失时兼容为 5 列 × 8 行，合法范围均为 3–20。行数只控制分页容量；列数改变时，`items.json` 内的 `gridSlot` 会按二维坐标迁移，避免把所有旧条目顺序压入历史空槽。

## 标识、时间与条目

- Tab 和条目 ID 是小写 UUID v4 字符串，创建后稳定不变。
- 时间戳为 UTC RFC 3339 字符串，例如 `2026-08-20T09:30:00Z`；没有时间时使用 JSON `null`。
- `type` 只允许 `application`、`file`、`folder`、`url`、`shortcut`。
- `arguments` 是字符串数组，每项表示一个逻辑参数；不得持久化为待 Shell 再解析的整行命令。
- `workingDirectory` 和 `icon` 是字符串或 `null`。
- `application`、`file`、`folder`、`shortcut` 的 `target` 以及非空的 `workingDirectory`、`icon` 可使用绝对路径、环境变量路径或普通相对路径；普通相对路径以 HLaunch EXE 所在目录为基准。
- `url` 的 `target` 保持 URL 原文，不进行文件系统路径转换；其可选工作目录和图标仍按路径规则处理。
- 禁止使用依赖当前驱动器或当前目录的盘符相对路径（如 `C:tools\\app.exe`）和根相对路径（如 `\\tools\\app.exe`）。
- `launchCount` 是非负 64 位整数；`lastLaunchedAt` 是时间戳或 `null`。
- `gridSlot` 是同一 Tab 内唯一的零基槽位，范围为 0–9999；缺失时按旧版数组顺序兼容补齐，删除条目不会压缩其他条目的槽位。

```json
{
  "schemaVersion": 1,
  "tabs": [{
    "id": "72976493-b090-4ca3-bbb8-d1d97d9f4eaf",
    "name": "默认",
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
      "lastLaunchedAt": null,
      "gridSlot": 0
    }]
  }]
}
```

## 数据根与便携模式

- 启动时默认在 HLaunch EXE 同目录创建并使用 `data` 子目录，不要求 `portable.flag` 或启动参数。
- 选择数据根前必须验证目录可创建，并用临时探针完成写入、回读和删除；已有 `config.json`、`items.json` 也必须可读写。探针文件关闭后立即删除，不作为持久数据。
- 只有 EXE 同目录的 `data` 无法创建或未通过读写验证时，才回退到 `%LOCALAPPDATA%\HLaunch`；回退目录同样必须完成读写验证，两处都不可用时启动失败并提示权限问题。
- 日志随最终数据根定位到 `logs` 子目录，图标缓存定位到 `cache\icons` 子目录；两种位置互不共用，缓存删除后可按需重建。
- `portable.flag` 不再是启用便携数据根的前提；保留 `--portable` 参数解析只为兼容既有命令和开机启动项，但它不能禁止权限失败时回退 AppData。
- 配置和缓存路径相对于数据根解析；条目的目标、工作目录和图标普通相对路径相对于 EXE 目录解析，禁止依赖进程当前工作目录。
- 实际使用 EXE 旁 `data` 时，保存新增、编辑和拖入条目只把 EXE 目录树内的绝对目标、工作目录和图标路径转为普通相对路径；回退 AppData 后不做该转换。跨盘或目录树外路径保持绝对路径，包含环境变量的原始表达式保持不变。
- 环境变量展开、参数引用和快捷方式解析集中在 Windows 平台层。

## 读取、兼容与校验

- 配置解析与 key 顺序无关；缺失可选字段使用文档默认值。Glaze 读取选项显式关闭未知 key 报错，并保留 UTF-8 校验。
- 同一受支持 schema 内的未知字段可忽略并记录诊断，但保存时不保证保留。高于当前支持的 schema 必须只读保护并显示文件类型、完整路径和“不覆盖”说明，随后停止启动，禁止降级覆盖。
- 对枚举、数值范围、字符串长度、路径和数组规模做校验。解析安全上限为单文件 8 MiB、嵌套 32 层、128 个 Tab、总计 10000 个条目；这些是拒绝异常输入的上限，不是 UI 推荐容量。
- 单个失效条目应隔离并报告，不丢弃其余有效数据。
- 当前只存在 schema v1，尚无已发布旧格式需要迁移；跨版本迁移暂不实现，在首次定义 schema v2 时与迁移语料一并设计。高于当前支持版本的文件继续只读保护，不能降级覆盖。
- 正式字段和范围以本目录 `schemas/` 下的 JSON Schema 为准；示例不能替代 schema 与迁移测试。
- `activation.screenEdge` 的 `thicknessDip`、`cornerSizeDip`、`dwellMs`、`pollMs`、`cooldownMs` 是已弃用的兼容字段，不再是可调设置；schema 保留其旧范围以读取旧文件，新保存统一规范化为固定值。

## 安全写入

写入流程必须是“同目录临时文件 → 刷盘并关闭 → 原子替换”，同时只保留一个最近成功版本的 `.bak`。失败时保留原文件，不得用空对象覆盖。启动时若主文件损坏，应提供从备份恢复或导出诊断信息的路径。

多个异步保存请求需要串行化或合并。正常退出以及 Windows 注销/关机确认后必须关闭保存工作线程并等待必要的最终提交；正常交互期间不在 UI 线程执行大文件 I/O。

## 配置示例

```json
{
  "schemaVersion": 1,
  "appearance": {
    "backdrop": "acrylic",
    "opacityPercent": 95,
    "gridColumns": 5,
    "gridRows": 8
  },
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
      "dwellMs": 180,
      "pollMs": 30,
      "cooldownMs": 100,
      "disableOnFullscreen": true,
      "foregroundProcessBlocklist": ["game.exe"],
      "foregroundProcessAllowlist": []
    }
  },
  "diagnostics": {
    "loggingEnabled": true
  }
}
```
