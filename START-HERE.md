# Velora 开发入口

当前仓库：[AxlumLab/Velora](https://github.com/AxlumLab/Velora)。项目介绍与快速开始见 [中文 README](README.md) / [English README](README.en.md)。

使用独立工作目录；生成文件放在 `build/`，依赖由 rhodium 恢复到 `.rhodium/`，不要从旧源码包复制构建缓存或生成代码。

## 开始之前

1. 阅读 [AGENTS.md](AGENTS.md)，了解对象模型、模块边界、所有权和当前工作规则。
2. 阅读 [docs/STATUS.md](docs/STATUS.md)，核对实际实现、已验证范围和剩余限制。
3. 从 [docs/IMPLEMENTATION-PLAN.md](docs/IMPLEMENTATION-PLAN.md) 选择当前批次，再阅读相关设计与实际代码。
4. 使用 [docs/BUILD.md](docs/BUILD.md) 的 CMake presets。当前接入期只编译和简单启动，完整回归、ASan 和截图验收留到统一阶段；保留已有测试。

## 当前任务背景

- 先完善 Windows；Android 与 Impeller 暂缓。
- 原统计范围内的 38 个功能组均有部分运行实现，Fluent 注册了 45 个正式 XAML 类型；完整兼容验收通过仍为 0。
- 后续重点是各组 API、对象内容/集合、绑定、完整模板/状态、输入/焦点、异步与回收生命周期，以及 Windows UIA。不要把接入数量清零当成完整迁移完成。
- WinUI 2.3 的算法、模板与资源直接内化到 `modules/fluent/`；`porting/` 是固定来源的参考输入，不进入产品构建或安装。具体来源、MIT 与差异见 [PORTING.md](modules/fluent/PORTING.md)。
- 使用普通 C++23 类、继承、virtual 与 RAII。不要恢复旧 COM 式对象包装、激活桥、逐控件投影或平行运行时。
- 默认只做本地分批提交；没有明确要求时不推送或改写历史。先检查并保留已有工作区改动。

## 可直接交给 agent 的任务说明

> 这是 Velora 的当前工作区。先读 AGENTS.md、docs/STATUS.md 与 docs/IMPLEMENTATION-PLAN.md，再核对相关设计和实际实现。当前只推进 Windows；38 个功能组已部分接入，但完整 WinUI 2.3 兼容验收尚未完成。按共享底座和控件族继续完善实际 API/行为，不注册占位控件，不新建影子对象或属性。只编译并做简单启动检查，保留已有测试，完整回归/ASan/截图统一留到最后。同步 STATUS.md，按主题本地 commit，不推送；每次结束按功能组口径报告尚未接入、部分接入及完整兼容验收数量。

总体架构、插件边界和 XAML 管线分别见 [DESIGN.md](docs/DESIGN.md)、[PLUGIN-SDK.md](docs/PLUGIN-SDK.md)、[XAML-TOOLCHAIN.md](docs/XAML-TOOLCHAIN.md)。
