# 当前状态与验证

基线日期：2026-11-09。来源为用户提供的 velora.zip，经本次清理后形成独立源码树。本文记录实际状态；DESIGN.md 和其他设计文档中的“待实现”不能视为交付。

## Windows 本机基线（2026-11-09）

当前按用户要求只推进 Windows，Android/Impeller 暂缓。环境为 Windows 10 build 19044、Visual Studio 2022 17.14.39、MSVC 19.44.35228、Windows SDK 10.0.26100.0、CMake 4.4.0-rc3、Ninja 1.13.2、Java 25.0.3、Python 3.11。依赖通过 tpkg 的锁定配置恢复，Debug/Release 使用各自 ANTLR runtime 导出。

- `core-check` 完整构建及公开头独立编译通过，CTest **25/25** 通过，包含 Windows 窗口、图形、调度和生命周期测试。
- `dev-debug` 完整构建通过，包含 ANTLR、反射、XAML、资源包、Gallery 和公开头检查。CTest 首轮 **38/39**，仅窗口空闲等待测试失败；修复该测试后定向连续 **5/5** 通过，其余 38 项已通过。未将首轮记录改写为一次全绿。
- `dev-release` 完整构建通过，CTest **39/39** 一次通过，包含完整生成链、Gallery 与 Release 安装消费测试（31.01s）。测试目标沿用现有 `/UNDEBUG` 保留断言，运行库保持 Release 编译。
- `asan` 完整构建通过，CTest 首轮 **24/25**，同一空闲等待测试失败；修复后定向连续 **5/5** 通过。已运行用例没有 ASan 内存错误报告。
- Gallery 自动测试通过，D2D 回读截图在 `build/dev/gallery-screenshots/`。独立 Gallery 也实际启动，通过 Win32 消息输入 `Ada中文`、点击 Greet、改变两种 CheckBox 绑定、ToggleSwitch、RadioButton、Slider 指针/方向键及滚轮，截图确认结果和汇总值；发送 WM_CLOSE 后进程退出码为 0。截图在 `build/dev/gallery-native/02-window.png`、`03-input-controls.png`、`04-scrolled.png`。
- `--live-ime` 已尝试，TSF 和 IMM 两条真实拼音路径均报告 `NOT_RUN (Microsoft Pinyin unavailable)`。WM_CHAR 中文输入与 TSF 后端初始化通过不能替代真实组合输入验收。
- CheckStyle.py、CheckWorkspace.py 和本轮 `git diff --check` 通过。

本轮修复：`velorac embed-pack` 原先将 unsigned char 按字符写入生成 C++，使 VPAK 魔数和二进制字节直接进入源码，导致首次 `cmake --build --preset dev-debug --parallel 4` 失败。现提升为无符号整数后输出，正常重新生成并构建嵌入包示例；安装消费测试也通过。窗口等待测试原先最多处理五批初始化消息，改为两秒内等待稳定，仍要求实际 80ms 等待耗时至少 70ms，不降低空闲等待断言。

本地证据位于忽略的构建目录：`build/dev/build-debug.log`（首次失败）、`build/dev/build-debug-fixed.log`、`build/dev/build-release.log`、`build/dev/debug-results.xml`（首轮）、`build/dev/release-results.xml`、`build/asan/asan-results.xml`（首轮）、各配置 CTest 的 `Testing/Temporary/LastTest.log`（最近运行）和 `build/dev/live-ime.log`。构建产物及临时操作脚本不纳入源码。

## 已保留的实现

### 可用程度评估（2026-11-09）

定位：可运行、可回归的 Windows UI 原型框架，适合基础表单和框架自身开发；尚不具备成熟通用应用 SDK 或完整 WinUI 2.3 替代品的交付条件。当前封闭 schema 有 18 种内建元素（包括布局容器），不是 18 个完整 WinUI 控件。未建立完整 API/状态/行为分母，不给出无证据的完成百分比。

| 层次 | 当前可用程度 | 主要缺口 |
| --- | --- | --- |
| Windows 宿主与绘图 | 窗口、D2D/DWrite、调度、基础 DPI/生命周期已有实测 | 完整文本编辑、真实 IME 组合输入和平台边界仍需完善 |
| 属性/绑定/资源/编译 | 主路径可运行，正式 XAML、生成代码、包及安装消费已有回归 | 统一可扩展元数据、类型工厂、全部模板语义未完成 |
| 控件与 C++ API | 基础布局和交互可用，实际类仍偏布局实现 | 常用类型化 API、事件对象接口、virtual 输入/绘制、真实控件继承体系 |
| 视觉与兼容 | 基础 Gallery 和少量 WinUI XAML 摘录 | 绝大多数复合控件、完整默认模板和状态、完整历史对照清单 |
| 辅助功能 | 有内部 AutomationNode/动作抽象 | 没有 Win32 UIA provider/WM_GETOBJECT 接入，不能宣称屏幕阅读器可用 |
| 发布 SDK | 静态应用及资源嵌入可构建 | UI 线程销毁、共享插件 SDK、裁剪与性能预算仍未交付 |

### 当前推进：Toggle API 与源码移植基线

- 新增 `createElement<T>(...)`，使用同一默认值初始化路径创建实际元素，不生成代理对象。
- `ToggleSwitchElement::isOn/setIsOn/isOnProperty`、`ToggleButtonElement::isChecked/setIsChecked/isCheckedProperty` 直接访问既有依赖属性。事件仍由宿主协调，未声称事件对象 API 已完成。
- Toggle 行为改从有效值通知触发，涵盖直接 C++、Framework、样式、表达式、clearValue 和绑定更新；跨注册表按属性 ID 识别同一属性，避免对象 API 写入后漏事件/绑定写回/单选互斥。
- Binding 在向目标发布值时抑制回写，并以短期共享持有和弱订阅支持事件中卸载目标；没有把 Binding 或控件复制成第二份状态。
- RangeBase 后续已迁到对象侧，见下一节；三态 CheckBox、完整独立控件 SDK、自定义输入/绘制和控件继承体系仍待实现。
- WinUI 本地源码 fork 基线为 `third_party/winui2.3/upstream/`，锁定 `v2.3.191129002` / `2237b09d9ba6052414ddccf3d61316de20564303`。完整 MIT 原文及 2930 个上游文件来源清单已保存。尚未接入新增上游控件或替换运行中的模板，不计为移植完成。
- 本轮 API 变更后：完整 `dev-debug` 构建及 **39/39** 回归通过（82.30s）；`asan` 重建并运行 controls/framework 两项相关测试 **2/2** 通过，含 TwoWay 源 setter 次数及事件中卸载绑定目标的新增断言。证据为 `build/dev/toggle-debug-results.xml`、`build/asan/toggle-asan-results.xml`。本轮没有重跑 Release，前述 Release 39/39 属于上一轮 Windows 基线。
- 加入许可证安装规则后，重新配置并复测安装消费用例通过；`share/licenses/Velora/WinUI2.3-LICENSE.txt` 与上游 MIT 原文 SHA-256 一致。

### 后续推进：RangeBase 对象 API 与远端仓库

- GitHub 仓库为 `https://github.com/FlybirdLab/Velora`，main 保留本地重新初始化的基线及远端原有 MIT 许可证提交，通过合并接续历史，没有强推。上游 WinUI 许可证仍单独保留。
- `SliderElement`、`ProgressBarElement` 共用 `RangeBaseElement` 的实际依赖属性；提供 Minimum/Maximum/Value 类型化访问及 Slider StepFrequency。Framework 不再单独执行范围校验或修正。
- 普通 C++、Framework、Binding/表达式和清除属性共用校验；依赖值在通知前一并修正，表达式订阅保留。新增用例覆盖非法数值/类型、遮蔽样式、clearValue、跨线程拒绝、TwoWay 写回和事件中卸载目标；Gallery 用例检查编译 XAML 创建的 Slider 类型化写入经 x:Bind 驱动 ProgressBar。
- 当前仍是破坏性截断，不保存被截断的原始值，也未实现完整 WinUI RangeBase 事件参数和 coercion 语义。不是新增上游控件移植完成。
- 本轮完整 `dev-debug` 构建与 **39/39** 回归通过（108.81s），含 Gallery 和安装消费；相关 `asan` controls/dependency_property/framework 三项重建并 **3/3** 通过（0.44s）。CheckStyle、CheckWorkspace、git diff 空白检查通过。未重跑 Release、全套 ASan 或原生 Gallery 消息验收。证据为 `build/dev/range-final-build.log`、`build/dev/range-debug-results.xml`、`build/asan/range-asan-results.xml`。
- 安装规则新增 Velora 自身 MIT 许可证。安装后的 `Velora-LICENSE.txt`、`WinUI2.3-LICENSE.txt` 分别与两份源码 LICENSE 的 SHA-256 一致。

### 后续推进：作用域事件订阅

- 新增 `Framework::subscribeEvent` 与可移动的 `EventSubscription`，基于既有事件注册表提供 RAII 退订；不新建事件树或控件对象。与替换式 `on()` 区分，多个订阅可独立共存，支持 sender、originalSource、Handled 和 handledEventsToo。
- `reset()`、作用域退出和移动赋值释放旧注册；回调中退订会使当前分发快照跳过已撤销的处理器。元素移除释放处理器捕获对象；句柄可晚于元素和 Framework 销毁，宿主生命周期标记被外部保留也不会导致访问已析构宿主。
- 句柄遵循 UI 线程亲和性，宿主存活时跨线程退订会 fail-fast。尚未实现自动投递销毁、控件对象上的事件访问器以及 virtual 输入/绘制，不将此项计为完整控件 SDK。
- 本轮完整 `dev-debug` 构建（含新公开头独立编译）及 **39/39** 回归通过（81.46s）；相关 `asan` controls/framework 重建并 **2/2** 通过（0.36s）。CheckStyle、CheckWorkspace、改动测试与新源文件格式检查、git diff 空白检查通过。未重跑 Release、全套 ASan 或真实窗口输入验收。证据在 `build/dev/events-build.log`、`build/dev/events-results.xml`、`build/asan/events-results.xml`。

| 范围 | 当前事实 |
| --- | --- |
| 控件对象 | UIElement/BuiltinElements、createElement、shared_ptr 元素树和 virtual 布局已有实现；一部分行为仍集中在 Framework |
| 属性与交互 | DependencyObject、属性优先级、Binding、TwoWay 写回、事件、焦点、范围值、滚动与内容生命周期代码保留 |
| XAML | 二进制 IR、读写、加载器、代码后台生成、资源/模板/状态相关代码保留；编译 schema 仍以内建控件为主 |
| 资源 | URI、限定条件、.vpri 索引、.vpak、压缩与 loose/packed 访问保留；包可嵌入应用 |
| Windows | Win32、D2D、DirectWrite 和应用窗口已有本机测试与截图；TSF 初始化通过，真实拼音组合输入尚未验收 |
| Gallery | 单页基础控件示例已整理，删除无作用导航和空页面；不是完整 WinUI 2.3 Gallery |
| 普通类型注册 | RuntimeTypeRegistry 记录 TypeId、类名、基类，校验重复/未知基类/继承环；没有插件工厂或动态加载器 |
| 元数据 | .rkmeta 写入 schema 5，普通 v3/v4 元数据仍可读；旧 abi-* 行不再接受 |

## 未实现或尚未完整验收

用户已明确 WinUI 2.3 完整行为覆盖与上游控件逻辑移植要求，执行原则见 IMPLEMENTATION-PLAN.md。基础验证不代表完整控件兼容验收。

- Android：没有完整平台宿主、Impeller 后端、NDK preset、IME/辅助功能适配。
- 动态控件插件：缺共享库 SDK、导出配置、插件匹配与加载、开放工厂及输入/绘制扩展契约。
- 源码按需裁剪：有模块拆分，不等于已经实现控件与资源的完整依赖裁剪。
- Gallery：复合控件、完整主题模板和历史 2.3 清单尚待补齐。Fluent 包只是部分资源，不能据此宣称完整视觉一致。
- C++ 开发体验：移除了包装，仍需把常用属性与控件行为整理成直观的类接口。
- 编辑器：没有可用文本编辑/编译/预览进程；文档给出重建式预览设计。
- 跨线程生命周期：当前 shared_ptr 与弱令牌不等于完整 UI 线程销毁协议，插件交付前必须落实并验证。

## 源码包清理阶段的历史验证（Linux）

环境：Linux、GCC 14.2.0、C++23、CMake 4.4.4、Ninja，Debug core-check，ANTLR 关闭。

- 完整 core-check 构建通过，含该配置下的公开头文件独立编译。
- CTest **17/17 通过**：基础类型/内存、反射消费与边界、数据、编码、元素树、合成、视觉桥、Framework、ScrollViewer、基础控件、HAL 骨架、资源 URI、资源包、类型注册、依赖属性、控件核心。
- CheckStyle.py、CheckWorkspace.py 通过。后者检查旧自建对象包装的路径/符号，不屏蔽真实 Windows COM。
- 编码测试额外以 Debug 和 `-O2 -DNDEBUG` 实际运行通过，覆盖非 BMP、嵌入 NUL、非法 UTF-8 与孤立代理项。
- 21 个不依赖生成 ANTLR 头的相关编译器/元数据/XAML 编译单元通过 GCC C++23 语法检查。
- 实际 MetadataCatalog 的独立检查通过：普通 v3/v4 兼容、v5 回环、旧 ABI 行拒绝，以及新测试输入对应的五类型元数据、跨类型属性路径和只读限制。
- 新增的三个反射测试输入作为 C++ 头文件通过语法与签名检查；对应两个测试编译单元语法检查通过。
- Gallery XAML 的 XML 结构、x:Name 与测试类型/事件引用进行了静态核对。

## 清理阶段的验证缺口与当前剩余限制

清理阶段没有准备 ANTLR 依赖，完整生成链、编译器 golden、安装消费和资源包 CMake 集成当时未执行；这些路径现已由 Windows Debug 构建和测试验证。

当前仍缺真实拼音 IME 组合输入、完整 WinUI 2.3 对照清单、辅助功能及全部控件状态验收。Android 暂缓。

下一步依据 IMPLEMENTATION-PLAN.md 固定 WinUI 2.3 参考与完善普通 C++ 控件语义；保留上述验证缺口，不把基础 Gallery 当成完整兼容交付。
