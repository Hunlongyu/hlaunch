# ADR-0002：使用 Glaze

- 状态：已接受
- 日期：2026-08-20

## 决定

使用 C++23 header-only 库 Glaze 处理 JSON。通过 CMake FetchContent 获取完整 commit hash 对应的 `v7.9.1`，产品只链接 `glaze::glaze`。只启用 JSON 所需能力，关闭 install、examples、EETF、SSL 和实验性 C++26 reflection。

## 原因与影响

Glaze 直接支持 C++ 聚合类型、标准容器和类型安全读写，与项目的 C++23 基线一致，不增加运行时 DLL。配置数据规模较小，因此采用 size 优化并关闭强制内联，优先控制编译时间和二进制体积。

自动反射不能成为磁盘 schema 的隐式来源。持久化层使用专用 DTO 或显式 metadata 固定字段名，并由单一适配层转换为领域对象。Glaze 默认遇到未知 key 会报错，而 HLaunch schema v1 允许忽略扩展字段，所以适配层必须显式设置 `error_on_unknown_keys=false` 并覆盖相应测试。

yyjson 的 C API 与手工生命周期不符合当前偏好；Jsonifier 的上游 CPU 配置和成熟度风险也没有带来本项目需要的收益，因此两者不作为现行依赖。
