# Velora

**简体中文** | [English](README.en.md)

**面向原生应用的 C++23 + 编译式 XAML UI 框架。**

Velora 希望提供接近 WPF/UWP 的属性、绑定、模板、资源与代码后台体验，以 WinUI 2.3 历史 Gallery 为视觉和交互目标。框架使用普通 C++ 类、继承、virtual 扩展点和 RAII，没有 Qt 依赖。

当前优先完善 **Windows**，后端为 Win32、Direct2D 和 DirectWrite。项目处于持续开发阶段，适合研究、原型和参与框架开发，尚未达到成熟通用应用 SDK 或完整 WinUI 替代品的交付状态。

仓库：[AxlumLab/Velora](https://github.com/AxlumLab/Velora) · 许可证：[MIT](LICENSE)

## 目录

- [当前状态与兼容边界](#当前状态与兼容边界)
- [已有能力与控件范围](#已有能力与控件范围)
- [架构与源码布局](#架构与源码布局)
- [Windows 快速开始](#windows-快速开始)
- [示例与开发入口](#示例与开发入口)
- [C++ 与 XAML 开发方式](#c-与-xaml-开发方式)
- [资源与发布](#资源与发布)
- [验证与贡献](#验证与贡献)
- [后续方向与文档](#后续方向与文档)
- [许可证与上游来源](#许可证与上游来源)

## 当前状态与兼容边界

以下为 **2026-11-11 的源码快照**。最新实现和每轮验证证据以 [STATUS.md](docs/STATUS.md) 为准。

| 项目 | 当前事实 |
| --- | --- |
| Windows | 原生窗口、D2D/DirectWrite、布局、输入、资源及多个示例已有运行路径 |
| Fluent 类型 | 正式 `.vtypes` 声明中有 **45 个 XAML 控件类型**，另有 C++ 服务、模型和接口 |
| 功能组接入 | 原统计范围内 **38 组均为部分实现**；未接入 0 组，完整兼容验收通过 0 组 |
| 验证 | 早期基线有回归记录；近期接入按当前开发安排仅编译和简单启动，未统一重跑完整回归 |
| Android | 计划支持；Impeller standalone、应用宿主和真机输入尚未交付 |
| 动态插件、编辑器 | 已有设计，完整共享运行时插件 SDK 和 XAML 编辑/预览产品尚未实现 |

**功能组接入不等于完整迁移。** 38 是固定源码区域的跟踪口径，包含 preview Scroller，不是 WinUI 2.3 全部公开类型或 API 的总数。模板、视觉状态、对象内容/集合、绑定、输入/焦点、异步生命周期、辅助功能与完整 Gallery 对照仍需继续完善。

Velora 面向新应用，不承诺 WPF/UWP 旧项目源码兼容、WinRT 二进制兼容，或跨任意编译器/框架版本的稳定 C++ 对象 ABI。Windows UIA provider、完整真实 IME 组合输入验收和非 Windows 产品后端仍有缺口。

## 已有能力与控件范围

基础管线包括依赖属性及有效值通知、绑定、模板/资源、布局、事件路由、焦点与指针捕获、绘制和合成。`controls` 提供通用 UI 底座；`fluent` 直接拥有内化后的控件实现。

| 控件族 | 当前类型或入口 |
| --- | --- |
| 基础布局与输入 | Grid、StackPanel、Canvas、Border、TextBlock、TextBox、Button、CheckBox、RadioButton、ToggleSwitch、Slider、ProgressBar、ScrollViewer、ListView 等通用元素 |
| 按钮、命令与图标 | DropDownButton、SplitButton、ToggleSplitButton、AppBarButton/AppBarToggleButton、CommandBar/AppBarSeparator、FontIcon、SymbolIcon |
| 菜单与弹出服务 | MenuBar/MenuBarItem、菜单项/切换项/单选项/分隔线，以及 Flyout、MenuFlyout、CommandBarFlyout、ContentDialog、TeachingTip、ToolTip 等 C++ 服务 |
| 编辑与选择 | NumberBox、AutoSuggestBox、ComboBox、RadioButtons、RatingControl、DatePicker、TimePicker、CalendarView、CalendarDatePicker、ColorPicker/ColorSpectrum |
| 导航与内容 | NavigationView/NavigationViewItem、TreeView 与 C++ TreeViewNode、TabView/TabViewItem、Pivot、FlipView、SplitView、TwoPaneView、PersonPicture |
| 滚动、手势与动效 | ScrollBar、Scroller、ItemsRepeater、SwipeControl/SwipeItem、RefreshContainer/RefreshVisualizer、ParallaxView、AnimatedVisualPlayer |

这些类型都有实际实现入口，但能力并不齐平。例如，ItemsRepeater 当前是固定高度的垂直窗口实现/回收，CalendarView 先支持公历单选，AnimatedVisualPlayer 接受普通 C++ 绘制源，尚未提供 Lottie 文件解码。日期/时间、刷新、滑动和颜色也保留明确的 API、模板及设备输入差异。详见 [移植说明](modules/fluent/PORTING.md)。

Windows 图标使用系统安装的 **Segoe MDL2 Assets**；跨平台字体替代方案暂未实现。

## 架构与源码布局

```text
XAML + C++ 代码后台 + .vtypes 元数据
                  │ 构建期 velorac
                  ▼
         .vxbf + 生成的连接代码 + .vpak
                  │ 正式组件加载
                  ▼
       UIElement / DependencyObject / ElementTree
                  │ 布局、输入、绘制与合成
                  ▼
          Windows：Win32 + D2D + DirectWrite
```

正常应用加载编译后的 XAML；ANTLR 用于构建期编译器。未知类型、属性和不支持的语法需要诊断，不能通过静默忽略实现“兼容”。每个控件保留一个实际对象和一份有效属性值。

| 目录 | 职责 |
| --- | --- |
| `modules/foundation` | 基础类型、容器和编码工具 |
| `modules/reflect`、`modules/runtime` | 元数据、类型注册、依赖属性和绑定 |
| `modules/resources`、`modules/xaml` | 资源 URI/索引/包、XAML 二进制及加载器 |
| `modules/controls` | 实际元素树、布局、输入、模板和通用控件底座 |
| `modules/fluent/src`、`modules/fluent/resources` | Fluent 行为、正式控件声明、编译模板和主题 |
| `modules/fluent/porting` | 锁定版本的待完善参考输入；不进入产品构建、安装或资源包 |
| `modules/graphics`、`modules/composition` | 绘图、文本服务、视觉树和合成 |
| `modules/platform`、`modules/application` | 平台服务、应用、窗口和 XAML 宿主 |
| `modules/markup` | 构建期反射/XAML 编译器 `velorac` |
| `examples`、`tests`、`cmake`、`scripts` | 示例、既有测试、构建辅助与源码检查 |

## Windows 快速开始

### 1. 获取源码

```powershell
git clone https://github.com/AxlumLab/Velora.git
cd Velora
```

旧工作区只需更新远端地址；不需要重新初始化或改写已有历史：

```powershell
git remote set-url origin https://github.com/AxlumLab/Velora.git
```

### 2. 准备工具链与依赖

需要 Visual Studio 2022 C++ 工具链、Windows SDK、CMake 3.25+、Python 3，以及完整构建使用的 Java 11+ 和 rhodium。`core-check` 使用 Ninja，并需要 x64 MSVC 开发终端；`dev` 使用 Visual Studio 2022 的多配置生成器。环境初始化方式见 [BUILD.md](docs/BUILD.md)。

第三方依赖由 [rhodium.lua](rhodium.lua) 和 [tpkg.lock.toml](tpkg.lock.toml) 锁定。先为 Debug 恢复并生成 ANTLR C++ runtime 导出：

```powershell
rhodium restore --toolchain windows-msvc-x64 --locked --config debug
rhodium generate --toolchain windows-msvc-x64 --config debug
```

生成文件应位于 `.rhodium/generated/cmake/rhodiumConfig.cmake`。如需 Release，再单独恢复对应 CRT 配置：

```powershell
rhodium restore --toolchain windows-msvc-x64 --locked --config release
rhodium generate --toolchain windows-msvc-x64 --config release --output .rhodium/generated/cmake-release
```

还需要 ANTLR **4.13.2 complete jar**。CMake 会检查锁定的 SHA-256，并尝试读取 `.rhodium/tools/antlr4-4.13.2-complete.jar`；未找到时在下一步指定本地路径。rhodium 的安装和源配置由使用者环境提供，仓库不携带依赖缓存。

### 3. 构建并运行 Fluent Gallery

```powershell
cmake --preset dev -DVELORA_ANTLR_TOOL_JAR=C:/tools/antlr-4.13.2-complete.jar
cmake --build --preset dev-debug --target velora_fluent_gallery --parallel 4
& ./build/dev/examples/fluent_controls/Debug/velora_fluent_gallery.exe
```

如果 CMake 已找到校验通过的 jar，可省略 `-DVELORA_ANTLR_TOOL_JAR=...`。构建会部署匹配的 `Velora.Fluent.vpak` 和 WinUI MIT 许可证到 Gallery 可执行文件旁。

当前批量接入的简单启动入口：

```powershell
& ./build/dev/examples/fluent_controls/Debug/velora_fluent_gallery.exe --preview-remaining
```

该参数只执行一次接入调用链并显示窗口，不是完整行为测试。更多入口见下一节；构建排错、校验和与 Release/ASan 配置见 [BUILD.md](docs/BUILD.md)。

### 4. 仅构建核心库

核心配置关闭 ANTLR，不需要先恢复 rhodium 或 jar；它不构建完整 XAML 工具链和 Windows 示例。

```powershell
cmake --preset core-check
cmake --build --preset core-check --parallel 4
python scripts/CheckStyle.py
python scripts/CheckWorkspace.py
```

Windows 需在 x64 MSVC 开发终端中执行，并保证 Ninja 在 PATH。非 Windows 核心构建可用于部分逻辑检查，不代表已有可用的 Linux、macOS 或 Android UI 后端。

## 示例与开发入口

| 示例 | 构建目标 | 用途 |
| --- | --- | --- |
| [Fluent Gallery](examples/fluent_controls) | `velora_fluent_gallery` | 当前 Fluent 控件族、编译 XAML、弹出服务和 C++ 对象内容 |
| [基础 Gallery](examples/gallery) | `velora_gallery` | 基础表单、绑定、布局与输入 |
| [源码控件](examples/custom_control) | `velora_custom_control` | ValuePad 的 virtual 输入/绘制及 `.vtypes` 声明 |
| [应用启动](examples/n04_startup) | `n04_startup` | 应用、窗口及编译页面宿主 |
| [安装消费](examples/consumer) | 独立 CMake 工程 | 开发用途的 `find_package(Velora CONFIG REQUIRED)` 消费示例 |

Fluent Gallery 支持 `--preview-datetime`、`--preview-refresh`、`--preview-tree`、`--preview-navigation`、`--preview-tabs` 等初始化入口。它们用于快速查看局部接入状态，不是完整 WinUI 2.3 Gallery 的验收结果。

## C++ 与 XAML 开发方式

以下是页面结构片段；完整宿主、类型注册和构建配置请参考 Fluent Gallery：

```xml
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:fluent="using:Velora.Fluent" Spacing="12">
    <fluent:PersonPicture DisplayName="Ada Lovelace" Width="64" Height="64"/>
    <fluent:NumberBox Minimum="0" Maximum="100" Value="42"/>
    <fluent:CalendarDatePicker Date="2026-11-11" PlaceholderText="Select a date"/>
</StackPanel>
```

构建侧通过 `velora_add_xaml(... TYPES .../FluentControls.vtypes)` 传入声明；运行侧需先挂载匹配的 Fluent 包，再将使用到的类型通过 `PersonPicture::registerType(ui)` 等接口注册到该 Framework。当前并非所有对象、集合、事件、Binding/x:Bind 表达式都能用于源码控件，不能由基础管线的支持范围推断各控件已完整支持。

同一实际控件也可直接用 C++ 创建和配置：

```cpp
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/PersonPicture.hpp>

auto avatar = velora::createElement<velora::PersonPicture>();
avatar->setDisplayName("Ada Lovelace");
avatar->setBadgeNumber(3);
```

挂载仍使用原 UI 树，例如 `ui.insert(avatar, parentId)`；此前需要完成资源挂载和上下文准备。类型化 API 访问的是实际依赖属性，不创建控件代理。当前树使用 `shared_ptr` 所有权，父回指和回调应避免强引用环；控件操作、退订和销毁受 UI 线程亲和性约束。

扩展方式和限制见 [DESIGN.md](docs/DESIGN.md)、[XAML-TOOLCHAIN.md](docs/XAML-TOOLCHAIN.md) 及 [PLUGIN-SDK.md](docs/PLUGIN-SDK.md)。

## 资源与发布

- `.vxbf` 是编译后的 XAML，`.vpri` 是内部资源索引，`.vpak` 是资源包；资源索引由工具链管理，应用无需手工维护。
- `VELORA_EMBED_RESOURCES=ON` 默认将应用资源包嵌入二进制；关闭后由构建辅助部署外部资源。应用资源嵌入不表示 Fluent Gallery 已将独立的 `Velora.Fluent.vpak` 合并进单文件。
- 当前有源码的模块构建为静态库，CMake 导出 `velora::controls`、`velora::fluent`、`velora::application` 等目标。构建与安装辅助是可用的开发入口，不代表插件 SDK 已定版。
- 静态自包含应用是目标发布方式之一，完整按需裁剪仍在后续计划中。动态插件计划要求宿主与插件共享同一运行时，并匹配 SDK 构建、编译器、CRT、架构和配置；加载器及完整共享库 SDK 尚未交付。

## 验证与贡献

当前接入阶段按项目安排仅编译并做简单启动，保留既有测试，完整回归、ASan、截图和行为对照留到统一验收。需要执行既有回归时，可使用 `ctest --preset core-check` 或 `ctest --preset dev-debug`；请先构建相应配置，并在结果中注明平台、配置和实际执行范围。

参与开发前阅读 [AGENTS.md](AGENTS.md) 和 [START-HERE.md](START-HERE.md)。C++ 使用 C++23、Allman 花括号、4 空格及 UTF-8/LF；源码放在 `modules/<module>/src/velora/`，生成代码留在构建目录。不要手改生成输出、删除测试掩盖回归或把占位实现标为完成。

`build/`、`.rhodium/`、本地编辑器设置、Python 缓存和备份不进入 Git。提交应按独立主题拆分；改动控件时同步真实状态与来源说明。

## 后续方向与文档

近期重点是补齐现有控件族的 API、内容/集合与绑定、模板/视觉状态、输入/焦点、异步取消/回收、UIA，以及完整历史 Gallery 的公开类型和行为清单。Android/Impeller、原生插件、编辑器预览和性能对照仍属于后续工作，没有已完成或稳定发布的承诺。

| 文档 | 内容 |
| --- | --- |
| [STATUS.md](docs/STATUS.md) | 当前实现、功能组统计、验证证据和缺口 |
| [BUILD.md](docs/BUILD.md) | 依赖、presets、构建及示例运行 |
| [DESIGN.md](docs/DESIGN.md) | 架构、模块、所有权和发布目标 |
| [IMPLEMENTATION-PLAN.md](docs/IMPLEMENTATION-PLAN.md) | 分批工作顺序与验收条件 |
| [XAML-TOOLCHAIN.md](docs/XAML-TOOLCHAIN.md) | 类型声明、编译、资源和预览设计 |
| [PLUGIN-SDK.md](docs/PLUGIN-SDK.md) | C++ 扩展、兼容和生命周期边界 |
| [PORTING.md](modules/fluent/PORTING.md) | WinUI 来源、内化差异与各族限制 |
| [CLEANUP.md](docs/CLEANUP.md) | 清理基线中删除、保留与迁移的记录 |

## 许可证与上游来源

Velora 使用 [MIT License](LICENSE)，保留源码中已有的版权归属。WinUI 派生实现和资源同时保留 Microsoft 版权及 [WinUI 2.3 MIT 原文](modules/fluent/LICENSE-WinUI2.3.txt)。仓库迁至 AxlumLab 不改写原作者的版权声明。

参考版本为 `microsoft/microsoft-ui-xaml` 的 `v2.3.191129002`，提交 `2237b09d9ba6052414ddccf3d61316de20564303`。具体文件来源、哈希及改写范围见 [PORTING.md](modules/fluent/PORTING.md) 和 [来源清单](modules/fluent/porting/Sources.json)。第三方依赖保留各自许可证。
