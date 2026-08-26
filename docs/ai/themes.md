# 主题与图形资源

## V1 格式

主题是目录，不是可执行扩展：

```text
Themes/Dark/
├── theme.json
├── background.png
└── preview.png
```

允许 JSON、PNG、JPEG、BMP 和 ICO。V1 不支持 ZIP、SVG、DLL、EXE、JavaScript、Lua 或 PowerShell。

## 解析边界

- `theme.json` 先由 Glaze 适配层解析成项目的 `Theme` 对象；UI 和 Renderer 不依赖 Glaze 或持久化 DTO。
- 所有资源路径必须在主题目录内。规范化路径后拒绝 `..` 逃逸、绝对外部路径和重解析点越界。
- 限制图片尺寸、解码后像素数和文件大小，避免恶意或错误主题耗尽内存。
- 缺失或无效令牌使用内置默认主题回退；不能导致 Launcher 无法显示。
- 默认主题应嵌入 EXE，确保外部主题损坏时仍可恢复设置。

## 渲染

- Direct2D 负责背景、Grid、Tab、状态、边框和动画。
- DirectWrite 负责 Unicode 文本与字体 fallback。
- WIC 负责图片解码；解码结果在后台准备，设备相关 bitmap 在渲染线程创建。
- DWM 效果按系统能力检测，不假定所有兼容 Windows 版本支持同一属性。
- 窗口材质与主题颜色分离：主题的 ARGB alpha 只控制背景、搜索框、卡片等表面；Solid、Mica、Acrylic、Tabbed 由 Platform 层请求 DWM。
- 文字、图标关键笔画和焦点指示默认保持不透明。透明表面必须保留可读性，不能为追求玻璃感降低必要对比度。
- Device lost 后从领域数据和解码缓存重建设备资源。

## V1 令牌

主题根对象包含 `schemaVersion: 1`、稳定 `id`、显示 `name` 和以下对象。正式类型及必填字段见 `schemas/theme.schema.json`。

| 分组 | 令牌 | 契约 |
| --- | --- | --- |
| `colors` | `background`、`surface`、`text`、`textMuted`、`accent`、`hover`、`pressed`、`border`、`focus` | `#RRGGBB` 或 `#AARRGGBB` |
| `metrics` | `cornerRadiusDip`、`itemWidthDip`、`itemHeightDip`、`gapDip`、`paddingDip` | 有界正数，单位 DIP |
| `typography` | `fontFamily`、`fontSizeDip` | 字体可为 `null`，缺失时使用系统 UI 字体 |
| `images` | `background`、`preview` | 相对主题根的字符串或 `null` |

高对比度开启时优先使用系统颜色并停用会削弱可读性的背景图片；自定义主题不能覆盖可见焦点、禁用状态和最低文本对比要求。未来增加令牌时，同 schema 内缺失字段使用内置默认值；不复用旧字段表达不同语义。
