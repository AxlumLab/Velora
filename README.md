# Velora

C++23 + XAML UI 框架，保留 WPF/UWP 开发体验，视觉与交互目标为 WinUI 2.3。第一批产品平台为 Windows、Android。

这是清理后的开发基线：旧 COM 式包装、生成桥接层和历史迁移指令已删除，实际 UI、编译器、资源管线与有效测试保留。**动态插件、Android 后端和完整 Gallery 尚未实现**。

## 从这里开始

- [START-HERE.md](START-HERE.md)：交给本地 agent 的入口。
- [AGENTS.md](AGENTS.md)：当前工程规则。
- [docs/STATUS.md](docs/STATUS.md)：源码现状与验证范围。
- [docs/DESIGN.md](docs/DESIGN.md)：产品、模块、生命周期与发布模式。
- [docs/PLUGIN-SDK.md](docs/PLUGIN-SDK.md)：普通 C++ 插件和兼容边界。
- [docs/XAML-TOOLCHAIN.md](docs/XAML-TOOLCHAIN.md)：元数据、资源、编译与预览。
- [docs/IMPLEMENTATION-PLAN.md](docs/IMPLEMENTATION-PLAN.md)：后续步骤和验收条件。
- [docs/BUILD.md](docs/BUILD.md)：依赖、构建与测试。
- [docs/CLEANUP.md](docs/CLEANUP.md)：本次删除、保留和修复。

## 源码结构

| 目录 | 职责 |
| --- | --- |
| modules/foundation、reflect、runtime | 基础工具、元数据、类型注册、依赖属性 |
| modules/resources、xaml | 资源索引/包、XAML 二进制与加载器 |
| modules/controls、fluent | 控件树、布局、绑定、事件和主题资源 |
| modules/graphics、composition | 绘图/文本服务、保留式视觉树 |
| modules/platform、application | 平台接入、应用与窗口生命周期 |
| modules/markup | 构建期反射/XAML 编译器 velorac |
| examples、tests、cmake、scripts | 示例、测试、构建和维护工具 |

无第三方依赖的核心检查：

```sh
cmake --preset core-check
cmake --build --preset core-check --parallel 4
ctest --preset core-check
python scripts/CheckStyle.py
python scripts/CheckWorkspace.py
```

完整 XAML 编译及 Windows 示例还需要 tpkg、ANTLR，见 BUILD.md。核心检查不证明窗口、DirectWrite、IME 或 Android 已可用。

源码保留原有版权与来源声明。本次没有替项目选择许可证，也不改变既有文件授权。
