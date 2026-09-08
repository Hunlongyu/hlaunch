# 版本与发布

## 触发与产物

- 工作流：`.github/workflows/release.yml`，仅监听标签推送 `v[0-9]+.[0-9]+.[0-9]+`。没有分支推送、PR、定时或手动构建入口。
- 校验作业先检查精确 `vX.Y.Z` 格式、无前导零、Windows 版本字段上限、CMake 版本和对应的非空更新日志。任一不符则不开始编译。
- x64、x86、ARM64 在 `windows-2025-vs2026` 上独立 Release 构建。x64、x86 运行 11 项不依赖交互桌面会话的 CTest；窗口/单实例、物理快捷键、托盘、Shell 启动四项保留本机或真机验证。ARM64 在 x64 主机交叉编译，不运行 ARM64 测试程序。
- 每份 EXE 都检查 PE 架构、内嵌 Manifest 的架构与版本、VERSIONINFO，以及是否误引入动态 CRT。发布作业再次检查三份文件齐全且 PE 架构与文件名匹配。
- Release 资产为 `HLaunch-X.Y.Z-x64.exe`、`HLaunch-X.Y.Z-x86.exe`、`HLaunch-X.Y.Z-arm64.exe`、`SHA256SUMS.txt`；程序仍无需安装或随附运行库。
- Release 正文是 `CHANGELOG.md` 对应版本段，包含中文和英文条目。发布权限仅授予最终发布作业，普通构建只有仓库读取权限。

## 用户说“发布新版本”时

该指令包含提交、标签推送和公开 Release 的授权。按以下步骤一次完成，不将后续步骤留给用户手工执行。

1. 检查工作区、当前分支、远端与最近发布标签，拉取远端信息。确认本次发布的实际改动范围；只提交相关路径，不夹带无关改动。若存在远端新提交，先正常整合，禁止强推。
2. 根据自上次发布以来的实际功能、行为变化和修复，整理 `CHANGELOG.md` 的 `[Unreleased]` 中英文条目。每条简短、面向用户，完整覆盖实际变化，不直接使用 Git 提交列表，不写未经验证的功能。
3. 运行 `python scripts/release.py prepare`。默认将当前 CMake 版本的补丁位加一，例如 `0.1.19 → 0.1.20`，归档日志为带日期的版本段，并留下空的 `[Unreleased]`。不可因重试重复递增；若已准备好版本，则继续该版本。用户指定版本时，以指定值同步 CMake 与日志，不再额外加一。
4. 运行 `python -m unittest discover -s tests -p test_release.py -v`、`python scripts/release.py validate --tag vX.Y.Z`，以及改动所需的构建与测试。`scripts/build-release.ps1 -Architecture x64` 可验证本机构建，另可选择 `x86` 或 `arm64`；缺少对应工具链或真机的验证限制必须记录，云端构建仍必须全部成功。
5. 使用中文 Conventional Commits 提交，例如 `chore(release): 发布 v0.1.20`。检查暂存范围及 `git diff --cached --check`。根 CMake 版本与发布日志必须在同一提交中。
6. 确认该标签在本地与远端均不存在，创建附注标签，例如 `git tag -a v0.1.20 -m "发布 v0.1.20"`。以原子推送一起上传发布分支和该确切标签，例如 `git push --atomic origin main refs/tags/v0.1.20`。不使用 `git push --tags`，不强制移动标签。
7. 跟进该标签对应的 Actions 运行直到完成。失败时读取失败日志并定位原因，不能把“标签已推送”当作发布成功。网络或 Runner 临时失败可重跑同一工作流；需要修改源码时，不移动已发布标签，应按新补丁版本修复并发布。
8. 成功后核对远端分支与标签指向、Release 正文、三份资产名称和大小。将资产下载到忽略的 `out/` 验证目录，回读并核对 SHA-256、PE 架构与程序版本，再向用户报告版本、下载链接和验证结果。

仅配置发布流程、提交代码或修改文档时，不运行 `prepare`，不创建标签，不创建 Release。

## 工具与来源

- `scripts/release.py`：Python 标准库实现版本准备、标签/日志校验与资产完整性检查；不执行 Git 写操作。
- `scripts/build-release.ps1`：通过 `vswhere -prerelease` 发现对应 MSVC，在独立目录构建和验证。
- GitHub Actions 官方的 [标签过滤语法](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#filter-pattern-cheat-sheet) 支持 `[0-9]+`，因此不需要用会误匹配后缀的 `v*`。
- [Windows 2025 / VS 2026 Runner 清单](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-VS2026-Readme.md) 包含 x86/x64 和 ARM64 C++ 工具链。
- 使用 [GitHub CLI Release 创建命令](https://cli.github.com/manual/gh_release_create) 的 `--verify-tag` 与 `--notes-file`，不隐式创建缺失的标签。
