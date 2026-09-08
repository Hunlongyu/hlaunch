# HLaunch AI 工作约定

本项目已有原生启动器实现。除非源码、测试或构建产物提供了证据，否则不得把 `docs/ai/` 中的规划描述成“已实现”。

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

## 版本与发布

- 用户说“发布新版本”或“发布版本”时，视为授权完成版本递增、更新日志、提交、打标签、推送和 GitHub Release 发布，并跟进 Actions 结果；无需逐步重复确认。
- “升级小版本”默认指补丁位加一，例如 `0.1.19 → 0.1.20`；用户指定版本时按其要求处理。仅正式发布时递增，日常功能修改、修复、文档修改与普通提交不递增、不打标签。
- `CMakeLists.txt` 的 `project(HLaunch VERSION ...)` 是唯一版本源。先根据实际改动整理 `CHANGELOG.md` 的 `[Unreleased]` 中英文条目，再运行 `python scripts/release.py prepare`；脚本一次递增补丁位并归档当次日志。日志覆盖全部实际用户可见变化，每条简短，不堆砌实现细节或测试过程。
- 发布前阅读并执行 `docs/ai/releasing.md`。使用中文 Conventional Commits、附注标签 `vX.Y.Z`，只推送本次发布标签，不使用 `git push --tags` 或强制覆盖标签。
- GitHub Actions 仅由 `vX.Y.Z` 版本标签推送触发；普通分支提交、PR 和其他标签不得编译。x64、x86、ARM64 三架构全部成功后才发布三份独立 EXE 和 SHA-256 校验文件。
- GitHub Release 正文必须来自 `CHANGELOG.md` 对应版本段，不用自动生成的提交列表代替更新日志。发布完成后核验远端提交、标签、Actions、三份资产与校验值，明确区分交叉编译与真机运行验证。
