# 构建与测试

以下命令从仓库根目录执行。当前仍为 C++23 静态库构建；动态插件所需共享库 SDK 和 Android 应用构建尚待实现。没有 Qt 依赖。

## 1. 核心检查

新增源码控件示例可在 Windows `dev-debug` 构建后运行 `build/dev/examples/custom_control/Debug/velora_custom_control.exe`。拖动数值区、Tab 聚焦、方向键或 Shift+方向键修改值。此示例专门验证 C++ 控件扩展，暂通过程序构建元素树；正式自定义类型的 XAML 注册尚未实现。

要求 CMake 3.25+、Ninja、支持当前 C++23 用法的编译器。Python 3 用于源码检查。

```sh
cmake --preset core-check
cmake --build --preset core-check --parallel 4
ctest --preset core-check
python scripts/CheckStyle.py
python scripts/CheckWorkspace.py
```

该配置关闭 ANTLR，因此不需要 tpkg 依赖，包含核心库、公开头独立编译以及 18 项非 Windows 行为测试（包含源码控件用例）。在 Windows 上会额外启用 Windows 专用测试，需要可用 Windows SDK 与交互桌面。Linux 的 graphics/platform 路径不可作为产品后端使用，核心通过不表示 Linux UI 已实现。

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

- Android 没有完整平台实现、NDK preset 或 Gradle 宿主。本包不能直接构建出 Android 产品。
- 当前模块默认 STATIC。插件共享运行时、导出配置和加载器仍是设计。
- 新环境必须从干净构建目录开始，不复用旧对象布局产生的缓存。具体已跑/未跑结果见 STATUS.md。
