# 构建与测试

以下命令从仓库根目录执行。当前仍为 C++23 静态库构建；动态插件所需共享库 SDK 和 Android 应用构建尚待实现。没有 Qt 依赖。

## 1. 核心检查

Fluent 示例新增 SplitView/TwoPaneView；`--preview-panes` 在启动时确认实际内容已挂载、侧栏展开、TwoPaneView 为 Wide。内容通过 C++ 对象属性连接，简单布局属性来自编译 XAML；页面可纵向滚动。此入口是启动检查，不是完整模式/输入验收。

Fluent 内部控件示例：构建 `dev-debug` 后运行 `build/dev/examples/fluent_controls/Debug/velora_fluent_gallery.exe`。CMake 自动部署匹配的 `Velora.Fluent.vpak` 与 MIT 许可证；示例包含头像、数字、弹出按钮、菜单、图标，以及 MenuBar、AppBar 按钮族、CommandBar 与分隔线。传入 `--open-menu` 预览独立菜单、`--open-menubar` 加载菜单栏菜单，或 `--open-commandbar` 在启动时确认前部命令按优先级成组溢出、末尾命令留在主区，再打开主/次命令溢出面板。按当前用户要求，开发期只做编译和启动检查，完整交互/回归统一在集成末尾执行。

ASan 的 Fluent 模板测试可复用同一编译资源包（不是复用 dev 的原生代码）：先构建 dev，再配置 `cmake --preset asan -DVELORA_FLUENT_TEST_PACKAGE=E:/Projects/velora/build/dev/modules/fluent/Velora.Fluent.vpak`。未提供资源包的核心配置只运行树外逻辑及缺失资源回滚检查，会明确报告模板检查未运行。

源码控件示例可在 Windows `dev-debug` 构建后运行 `build/dev/examples/custom_control/Debug/velora_custom_control.exe`。拖动数值区、Tab 聚焦、方向键或 Shift+方向键修改值。`SourcePage.xaml` 通过 `TYPES ValuePad.vtypes` 编译并打包，窗口注册工厂后使用正式组件加载器创建控件。当前自定义类型编译只支持首批字面值语义，详细范围见 XAML-TOOLCHAIN.md。

要求 CMake 3.25+、Ninja、支持当前 C++23 用法的编译器。Python 3 用于源码检查。

```sh
cmake --preset core-check
cmake --build --preset core-check --parallel 4
ctest --preset core-check
python scripts/CheckStyle.py
python scripts/CheckWorkspace.py
```

该配置关闭 ANTLR，因此不需要 tpkg 依赖，包含核心库、公开头独立编译以及 21 项非 Windows 行为测试（包含源码控件、Fluent 控件逻辑及类型注册/二进制加载用例；编译器用例在 dev 配置中运行）。在 Windows 上会额外启用 Windows 专用测试，需要可用 Windows SDK 与交互桌面。Linux 的 graphics/platform 路径不可作为产品后端使用，核心通过不表示 Linux UI 已实现。

如果 Ninja 未在 PATH，配置时传入实际路径：

```sh
cmake --preset core-check -DCMAKE_MAKE_PROGRAM=/absolute/path/to/ninja
```

Windows 上的 `core-check` 使用 Ninja，需要先进入 x64 MSVC 开发环境。可直接打开 Visual Studio 的 Developer PowerShell，或在普通 PowerShell 中运行（安装目录由 vswhere 查询）：

```powershell
$vsInstall = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module "$vsInstall/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vsInstall -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
```

## 2. Windows 完整开发构建

要求 Visual Studio 2022 C++ 工具链、Windows SDK、Ninja、CMake 3.25+、Java 11+ 和用户环境中的 tpkg。tpkg 的安装与源配置由其自身环境提供，本包不携带缓存或依赖二进制。

在支持 MSVC 的开发终端中，为 Debug 与 Release 分别恢复并生成依赖。以下沿用项目原构建命令与当前 VeloraDependencies.cmake 的导出路径：

```powershell
tpkg restore --toolchain windows-msvc-x64 --locked --config debug
tpkg restore --toolchain windows-msvc-x64 --locked --config release
tpkg generate --toolchain windows-msvc-x64 --config debug
tpkg generate --toolchain windows-msvc-x64 --config release --output .tpkg/generated/cmake-release
```

必须存在 `.tpkg/generated/cmake/tpkgConfig.cmake`，Release 导出目录为 `.tpkg/generated/cmake-release/`。检查它们确实指向对应配置的 ANTLR C++ runtime，不混用 MSVC Debug/Release CRT。tpkg.lua 和 tpkg.lock.toml 是依赖依据。

另需 ANTLR 4.13.2 complete jar，Java 用它生成解析器。模块要求 SHA-256：

```text
eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76
```

仓库会尝试读取 tpkg 下载缓存中的该文件，找不到时显式指定其本地路径，不跳过校验：

```powershell
cmake --preset dev -DVELORA_ANTLR_TOOL_JAR=C:/tools/antlr-4.13.2-complete.jar
cmake --build --preset dev-debug --parallel 4
ctest --preset dev-debug
cmake --build --preset dev-release --parallel 4
ctest --preset dev-release
```

完整配置构建 velorac、生成 XAML/反射代码、基础 Gallery 及测试。需要原生窗口的测试在可交互桌面运行。失败必须按实际原因处理，不把图形测试删掉或改成空成功。

`velora_gallery_test` 会将 D2D 回读截图写入 `build/dev/gallery-screenshots/`。实际示例为 `build/dev/examples/gallery/Debug/velora_gallery.exe`。平台测试默认不发送真实键盘输入；在可交互桌面上运行 `build/dev/Debug/velora_platform_window_test.exe --live-ime` 可尝试 Microsoft Pinyin 的 TSF/IMM 组合输入。若输出 `NOT_RUN`，即使进程退出码为 0，也不能记作真实 IME 验收通过。

Windows 内存检查配置不依赖 ANTLR：

```powershell
cmake --preset asan
cmake --build --preset asan --parallel 4
ctest --preset asan
```

## 3. 生成代码与资源

`velora_add_xaml` 是统一页面入口，生成 `.vxbf` 与连接代码。应用默认嵌入 `.vpak`，选择 `EXTERNAL` 时部署到可执行文件旁的 `Resources/`。`.vpri` 是内部资源索引，不能因为选嵌入包就删除限定条件和资源解析。

旧 `velora_xaml_compile` 的直接 C++ 构树路径仍用于回归。新功能以二进制管线为准，避免维护第二份语义。所有构建生成物放 build/<preset>/，不要复制回源码。

## 4. 尚不能执行的目标

Fluent 示例支持 `--preview-rating`：启动时加载 RatingControl 的编译模板、显示 ToolTip 并确认焦点保留。只观察窗口出现并关闭即可完成当前批次的简单启动检查；这不覆盖评分输入或完整提示交互。

`--open-commandflyout` 启动时打开主/次命令面板并确认实际命令对象已挂载；普通启动可点击 Open command flyout 按钮。当前只做窗口出现和关闭检查，完整输入验收留到最后。

`--preview-choices` 启动时打开 ComboBox，确认下拉状态和两个选择控件的初始项。Items 在示例 C++ 中接入，XAML 创建控件并设置初始 SelectedIndex。当前只做简单启动和关闭，不执行完整选择交互验收。

`--open-dialog` / `--open-teachingtip` 分别在启动时打开 ContentDialog / TeachingTip 的 C++ 服务及编译模板；普通启动通过 Open dialog / Show teaching tip 按钮进入。当前仅检查窗口启动、显示及关闭，取消关闭、结果、轻关闭和键盘交互留待统一验证。

`--preview-pages` 启动时确认 Pivot 的初选编辑页和 FlipView 的首项已实际挂载。页面集合通过 C++ 接入，控件及初始 SelectedIndex 由编译 XAML 创建；当前只做窗口启动/关闭，完整页面切换和状态保持留待统一验收。

`--preview-tabs` 启动时确认 TabView 初选文档标签及实际编辑器挂载，欢迎标签不可关闭。普通启动通过添加/关闭按钮使用示例应用的集合回调；运行时只发布请求。当前仅做启动/关闭，完整集合编辑、通知顺序和键盘/焦点验收留到最后。

- Android 没有完整平台实现、NDK preset 或 Gradle 宿主。本包不能直接构建出 Android 产品。
- 当前模块默认 STATIC。插件共享运行时、导出配置和加载器仍是设计。
- 新环境必须从干净构建目录开始，不复用旧对象布局产生的缓存。具体已跑/未跑结果见 STATUS.md。
