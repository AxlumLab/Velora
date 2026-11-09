# WinUI 2.3 源码移植基线

- 上游：<https://github.com/microsoft/microsoft-ui-xaml>
- 标签：`v2.3.191129002`
- 提交：`2237b09d9ba6052414ddccf3d61316de20564303`
- 许可证：MIT，原文见 [LICENSE](LICENSE)，源文件内版权声明保留。
- `upstream/`：该提交的完整 Git archive 源码快照，2930 个文件，未包含嵌套 `.git`。其中 `build/` 是上游构建脚本，`dev/Generated/` 是上游已跟踪的文件，均不是 Velora 的构建产物。
- `UPSTREAM.json`：原始 archive 文件的 SHA-256 清单；上游 `.gitattributes` 在 Git checkout 时可能改变文本换行，清单描述上游归档字节。

本目录是用户授权的本地源码 fork 基线，不是已接入运行时的控件库，也没有创建远程 GitHub fork。未将其 MSBuild 项目接入 Velora CMake。运行时依赖仍由 tpkg 管理；源码获取尝试 tpkg 后因 Windows 长路径失败，改用开启 `core.longpaths` 的 Git 按上述标签浅克隆、校验提交并导出。

采用允许破坏上游接口的移植：从此基线提取实际控件 C++ 和 XAML 到对应 Velora 模块，替换 WinRT/生成接口及 UWP 平台调用；复用其控件算法、模板与状态设计，由 Velora 实现依赖属性、布局、输入、渲染及宿主逻辑。每个派生文件保留版权声明，并注明上游路径、提交与本地改动。新实现进入正常构建及行为测试之后才算迁移完成。

注意 `dev/CheckBox`、`dev/Slider` 等目录包含主题 XAML 和测试页面，但不包含这些系统控件的完整 C++ 行为实现。`dev/SplitButton`、`dev/NavigationView` 等才有库控件实现。不得将全部主题文件存在当作全部控件逻辑可直接复制。

本轮未更换当前运行中的模板，尚未完成任何新增上游控件的运行时移植。下一步先完成模板/输入等依赖审计，再选择可验收的第一个控件。完整兼容清单仍需包含系统控件及对应历史 Gallery，不能由源码目录数代替。

分发派生代码及资源时保留 Microsoft 版权与 MIT 许可文本；SDK 安装包会安装本目录的 LICENSE。单独带许可证的第三方资产仍按其原声明处理。
