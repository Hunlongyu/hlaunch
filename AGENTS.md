# HLaunch AI 工作约定

本项目当前处于设计阶段。除非源码、测试或构建产物提供了证据，否则不得把 `docs/ai/` 中的规划描述成“已实现”。

## 阅读顺序

1. 先读 `docs/ai/README.md`。
2. 再按任务读取该索引列出的主题文档，不要默认加载全部文档。
3. 涉及产品范围时，以 `docs/ai/product-scope.md` 为准。
4. 涉及跨模块边界时，以 `docs/ai/architecture.md` 为准。
5. 涉及未定方案时，以 `docs/ai/open-decisions.md` 为准，不得擅自把待决项变成既定事实。

## 修改规则

- 使用 C++23、Unicode Win32 API 和 Target-based CMake。
- UI 线程只处理窗口、输入、布局和渲染；后台结果必须投递回 UI 线程后才能操作 HWND。
- COM 接口使用 `winrt::com_ptr`；经典 Win32 资源使用 WIL RAII。
- 配置解析库不得泄漏到领域层。
- 新增第三方依赖前更新 `docs/ai/engineering.md` 中的依赖决策。
- 行为、配置或数据格式变更必须同步对应 AI 文档与测试契约。
- 优先保持既有数据兼容；破坏兼容时必须提供迁移方案。
