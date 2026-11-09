# 新工作区入口

解压到新的空目录并打开 velora/。不要覆盖旧工作区，也不要带入旧 build、生成代码、阶段报告或补丁包。

## 交给本地 agent

可直接发送：

> 这是 Velora 清理后的唯一工作基线。先读取 AGENTS.md、docs/STATUS.md、docs/DESIGN.md、docs/PLUGIN-SDK.md、docs/XAML-TOOLCHAIN.md、docs/IMPLEMENTATION-PLAN.md、docs/BUILD.md。按 IMPLEMENTATION-PLAN 的第 0 步，核对真实构建和测试，不直接重写整个框架。项目方向是 C++23 + XAML、Windows + Android、WinUI 2.3 体验、普通 C++ 控件扩展。旧 COM 式稳定 ABI 已放弃，不得恢复。先报告当前环境能验证什么和实际问题，再完成可运行基线及文档同步。所有“完成”必须对应代码和实际验证。

## 阅读与判断

1. STATUS.md 区分既有行为与未来设计。
2. DESIGN.md 约束产品和模块，PLUGIN-SDK.md、XAML-TOOLCHAIN.md 说明扩展与工具链。
3. IMPLEMENTATION-PLAN.md 给出下一步目标、范围和验收。
4. BUILD.md 是构建入口。先运行相关核心检查，再处理 Windows 全量编译和实机验收。

Framework 仍集中了一部分控件行为，内建控件 schema 也仍然封闭。清理包装不等于最终 C++ 控件 SDK 已完成，后续应在保持现有工作路径的前提下逐步开放扩展点。

不要从旧压缩包覆盖回 InterfaceRef、ControlBridge、rkgen_abi、旧 AGENTS 或历史计划，它们是有意删除。变更记录见 docs/CLEANUP.md。
