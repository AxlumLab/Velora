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
| 控件与 C++ API | 基础布局和交互可用，已接入基本 virtual 输入/绘制 | 常用类型化 API、事件对象接口、完整输入语义、真实控件继承体系 |
| 视觉与兼容 | 基础 Gallery 和少量 WinUI XAML 摘录 | 绝大多数复合控件、完整默认模板和状态、完整历史对照清单 |
| 辅助功能 | 有内部 AutomationNode/动作抽象 | 没有 Win32 UIA provider/WM_GETOBJECT 接入，不能宣称屏幕阅读器可用 |
| 发布 SDK | 静态应用及资源嵌入可构建 | UI 线程销毁、共享插件 SDK、裁剪与性能预算仍未交付 |

### 当前推进：Toggle API 与源码移植基线

- 新增 `createElement<T>(...)`，使用同一默认值初始化路径创建实际元素，不生成代理对象。
- `ToggleSwitchElement::isOn/setIsOn/isOnProperty`、`ToggleButtonElement::isChecked/setIsChecked/isCheckedProperty` 直接访问既有依赖属性。事件仍由宿主协调，未声称事件对象 API 已完成。
- Toggle 行为改从有效值通知触发，涵盖直接 C++、Framework、样式、表达式、clearValue 和绑定更新；跨注册表按属性 ID 识别同一属性，避免对象 API 写入后漏事件/绑定写回/单选互斥。
- Binding 在向目标发布值时抑制回写，并以短期共享持有和弱订阅支持事件中卸载目标；没有把 Binding 或控件复制成第二份状态。
- RangeBase 后续已迁到对象侧，见下一节；三态 CheckBox、完整独立控件 SDK、自定义输入/绘制和控件继承体系仍待实现。
- 当时将 WinUI 完整快照放在 `third_party/winui2.3/upstream/`；按用户随后纠正，此镜像现已移出受管源码，改为直接在 Fluent 内化实现。历史快照仅保留在本机忽略的 build/reference 中，当前来源及 MIT 见 modules/fluent/PORTING.md。
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
- 句柄遵循 UI 线程亲和性，宿主存活时跨线程退订会 fail-fast。自动投递销毁及控件对象上的事件访问器仍待实现；基本 virtual 输入/绘制后续已接入，见下一节。
- 本轮完整 `dev-debug` 构建（含新公开头独立编译）及 **39/39** 回归通过（81.46s）；相关 `asan` controls/framework 重建并 **2/2** 通过（0.36s）。CheckStyle、CheckWorkspace、改动测试与新源文件格式检查、git diff 空白检查通过。未重跑 Release、全套 ASan 或真实窗口输入验收。证据在 `build/dev/events-build.log`、`build/dev/events-results.xml`、`build/asan/events-results.xml`。

### 后续推进：源码控件输入与绘制

- `UIElement` 增加基本指针按下/移动/释放、KeyDown、焦点资格和绘制 virtual 入口。`InputContext` 提供当前元素的坐标、焦点及捕获服务；路由输入携带局部/窗口 DIP 坐标和键盘修饰键。对象回调先于当前 sender 的实例处理器，并遵循 Handled/handledEventsToo。
- 指针捕获由指针入口决定目标，通用事件不再被捕获对象重定向；新增回归覆盖捕获期间的焦点、键盘和 Toggle 状态事件来源。回调临时持有实际对象，允许输入回调中移除树节点并释放调用方最后引用。
- `renderOverride` 可接管内容绘制，背景、子视觉、属性无效化和不可变 DisplayList 继续沿用现有管线。新增 `ValuePad` 示例及共用源码的回归测试，验证布局、拖动、键盘、Tab、禁用祖先、重绘和卸载。没有增加影子属性或新元素树。
- Windows 独立示例实际启动，通过 Win32 消息检查拖动、捕获后的区域外移动、释放、End/Left；截图确认最终显示 `Value 99`，WM_CLOSE 后退出码为 0。截图在 `build/dev/extensions-native/01-start.png`、`02-after-input.png`，会话记录在同目录 `session.json`。这是单主指针消息验收，不代表触摸、多指或全部输入设备验收。
- 此轮示例当时沿用 UserControl schema 身份，C++ 工厂挂载；源码声明与正式自定义 XAML 的首个切片后续已接入，见下一节。内建输入行为仍有 Framework 分派；完整卸载扩展、自动 UI 线程销毁、指针取消及多指输入仍待实现，不将 1C 或最终 SDK 标记为完成。
- 本轮完整 `dev-debug` 构建（含新公开头独立编译及独立示例）与 **40/40** 回归通过（90.37s）；相关 `asan` custom_control/controls_core/controls/framework/visual_bridge 五项重建并 **5/5** 通过（1.28s）。源码布局、工作区、15 个改动 C++ 文件格式与 git diff 空白检查通过。未重跑 Release、完整 ASan 或真实 IME 验收。
- 首轮构建曾重叠使用同一 dev 输出目录，随后出现 LNK1163 COMDAT 链接错误；结束重叠构建、清除两份受影响对象并重编后完整通过。失败记录保留在 `build/dev/extensions-full-build.log`、`extensions-rebuild.log`；最终证据为 `build/dev/extensions-object-rebuild.log`、`extensions-results.xml`、`build/asan/extensions-results.xml`。

### 后续推进：源码类型声明与编译 XAML

- 新增 `XamlTypeCatalog`，读取有版本的 `.vtypes` 并在完整校验后发布声明；首版支持内建基类及 Double/Integer/Boolean/String 的 Paint 属性。`using:Demo` 的类型解析、普通字面值、继承普通属性和 x:Name 已接入，未知类型/属性及不支持的表达式仍有诊断。
- Framework 的 `SourceControlRegistry` 绑定声明、实际 C++ 工厂及依赖属性描述符，拒绝重复或不匹配的注册。ValuePad 的类名/TypeId 现在为 `Demo.ValuePad`；其依赖属性名称、种类和默认值与编译器读取同一份声明，未新增影子对象、影子属性或 ElementKind。
- 引用自定义类型的 VXBF 1.4 保存实际引用类型的契约，新增无关源码类型不使原页面失效；内建页面继续写 1.3，1.0–1.3 仍按原格式读取。原 schema fingerprint、校验和、大小限制和结构校验保留，未知新版本拒绝。
- 组件加载使用对应 Framework 的类型目录，核对契约与工厂输出；工厂异常、类型缺失或属性赋值失败转为带位置的错误并回滚。回归覆盖已有根值恢复、重复声明、契约变更、无关类型注册及工厂内注册时的生命周期。
- `SourcePage.xaml` 通过 `velora_add_xaml(... TYPES ValuePad.vtypes)` 编译、生成连接代码并打包；示例窗口注册工厂后从正式资源组件创建 ValuePad。该 `.vtypes` 同时嵌入 C++，运行期不需要 XML/ANTLR。
- 当前只支持源码叶控件的首批字面值语义，源码控件的 Binding/x:Bind、资源表达式、模板、内容、事件声明、自动反射声明生成及二进制插件加载仍待实现，不将 2A 或完整 WinUI 兼容计为完成。
- 本轮完整 `dev-debug` 构建与 **41/41** 回归通过（91.88s），含 velorac 生成的源码控件组件、既有 Gallery、旧格式回归及安装消费。相关 `asan` source_control/custom_control/framework 三项重建并 **3/3** 通过（0.46s）。源码布局、工作区及 20 个改动 C++ 文件格式检查通过。未重跑 Release、完整 ASan 或真实 IME。
- 编译 XAML 后的独立窗口再次通过相同 Win32 输入检查，截图确认 `Value 99`，WM_CLOSE 后退出码为 0；证据在 `build/dev/source-types-native/`。构建和测试记录为 `build/dev/source-types-final-build.log`、`source-types-results.xml`、`build/asan/source-types-results.xml`。
- 全套回归后进一步将示例声明改为按名称查找并在缺失时抛出诊断，定向重建后 Debug **2/2**、ASan **3/3** 复测通过（0.10s、0.42s），独立窗口再次验证通过。记录为两个配置下的 `source-types-sample-build.log`、`source-types-sample-results.xml`。之后仅更新注释和文档。

### 当前推进：Fluent 内化 PersonPicture

- `modules/fluent` 现在拥有控件 C++ 行为，依赖 controls 的通用属性/树/布局/输入/模板底座。首个实现为 PersonPicture：初始字母算法、显式 Initials 优先级、Group/Photo/Initials/NoPhotoOrInitials、图片/数字/字形徽章优先级、负数字与 `99+`、尺寸/字体比例均有实际行为。
- 默认 XAML 模板、主题和声明直接位于 Fluent，正常编译进入资源包；不存在上游 WinRT 运行时或原仓工程依赖。完整 MIT 安装到 SDK，也放入 Fluent 资源包。整仓镜像已从当前源码树移除，原文件哈希及移植差异保存在 Fluent 模块。
- 由该控件需要补齐 Framework 挂载/卸载回调、自动化名称/类型扩展，图片 UniformToFill 和圆角子视觉裁剪。挂载失败回滚；实际模板更新时可在回调中卸载控件并释放最后引用。模板名称域按实例隔离，Light/Dark/HighContrast 切换有回归。
- Windows Fluent 示例实际显示 `AL`、`GH`、默认人像、群组、数字/字形/`99+` 徽章；Win32 消息验证群组切换、徽章 7→8、恢复缩写，关闭后退出码 0。截图与会话在 `build/dev/fluent-person-native/`。
- 完整 `dev-debug` 构建及 **42/42** 回归通过（96.88s），含编译 XAML、Fluent 资源包和 SDK 安装消费。相关 ASan 回归 **4/4** 通过，Fluent 测试加载实际资源包，另已复测挂载失败回滚及模板回调中释放最后外部引用。15 个改动 C++ 文件格式、CheckStyle、CheckWorkspace 与 diff 检查通过。构建日志为 `build/dev/fluent-internal-build.log`，CTest 结果为 `build/dev/build/dev/fluent-internal-results.xml`、`build/asan/build/asan/fluent-internal-results.xml`；未重跑完整 ASan、Release 或真实 IME。
- 这是内部控件的可运行切片，尚未声明完整 WinUI PersonPicture 兼容：Contact/PreferSmallImage/异步联系人照片、完整字体及模板替换契约、多语言辅助功能与 Windows UIA provider 等仍待实现；几何默认图标和部分系统调色板有明确差异，详见 `modules/fluent/PORTING.md`。

### 后续推进：批量引入 Fluent 待移植输入

- 按用户要求改为先批量引入控件相关输入、再逐族改写。`modules/fluent/porting/` 引入固定提交下 49 个控件/支持目录，以及公共 IDL、紧凑密度样式，共 1243 个文件；不是完整上游仓库，未引入上游工程、生成代码、激活运行时或测试应用。
- `Sources.json` 区分 `source-staged` 与正式实现；原始及规范化后文件均有 SHA-256。`StageFluentSources.py` 先核验原始来源再写入，支持不依赖参考缓存的 `--check`，拒绝覆盖修改后的文件。版权注释与模块 MIT 保留。
- 这些输入尚未编译、安装、打包或注册；可运行控件范围没有因此扩大。直接内化时用现有 Velora 底座替换依赖，不引入通用 WinRT 兼容层。系统提供逻辑的模板目录及实验 API 仍需单独实现/审查，不能从文件存在推断完整兼容。依赖替换范围与下一批顺序见 `modules/fluent/PORTING.md`。
- 本轮逐文件来源/导入哈希校验、重复导入一致性、CheckStyle 和 CheckWorkspace 通过；Windows Debug 的 fluent_controls/source_control/fluent 定向回归 **3/3** 通过（0.18s），日志 `build/dev/fluent-staging-tests.log`。本轮未改变运行时代码，未重复完整构建、完整回归或 ASan；这些未编译的输入没有通过编译验收。

### 当前接通：NumberBox 内联编辑

- NumberBox 已从待移植输入改写为 Fluent 实际 C++ 对象、共享类型声明和编译模板，Windows 示例正式 XAML 可创建。支持数值/文本同步、最小最大值修正、禁用验证、循环增减、空值 NaN、表达式、标题与提示、Hidden/Inline 增减按钮；没有加入第二份属性存储或上游 WinRT 运行时。
- 提交草稿通过 Enter/失焦，Escape 恢复数值显示；Up/Down、PageUp/PageDown 和实际按钮接入现有输入与事件路由。增加 Windows Escape 映射。ValueChanged 使用实际属性通知及可移除 token；C++、Framework、样式、TwoWay 写回和编译 XAML 均有覆盖。资源缺失回滚、重新挂载、提交回调中卸载并释放最后外部引用有回归。
- 表达式算法保留上游优先级和左结合规则，改为有界解析；支持 invariant decimal/科学计数法，明确拒绝除零、非有限结果和错误括号。格式化当前为最多 12 位有效数字，与原数字格式服务仍有差异。Compact Popup、RepeatButton 长按、滚轮、本地数字格式、完整模板/状态和 UIA pattern 均未完成，详见 Fluent PORTING.md。SplitButton/DropDownButton 仍需 Flyout/Popup 底座，不计为已接通。
- Windows 实际窗口验证 `2+3*4` → Enter 显示 `14` → Up/PageUp 显示 `25` → 无效草稿 Escape 恢复 `25` → 点击增量显示 `26`；关闭退出码 0。截图与会话为 `build/dev/fluent-numberbox-native/`。
- 完整 Debug 构建（含新增公开头独立编译）及 **42/42** 回归通过（109.45s，含 SDK 安装消费），日志 `build/dev/numberbox-full-build.log`、`numberbox-full-tests.log`、`numberbox-results.xml`；相关 ASan framework/controls_core/fluent_controls **3/3** 通过（1.20s），实际加载 Fluent 模板，日志 `build/asan/numberbox-tests.log`。格式 8 个改动 C++ 文件、来源哈希、CheckStyle、CheckWorkspace 和 diff 检查通过。未重跑 Release、完整 ASan 或真实 IME。

### 当前批量接通：弹出按钮族与菜单族

- 按最新要求改为按共享底座/控件族批量推进，不再逐个控件收尾。已接入窗口内单层 Popup/Flyout、DropDownButton、SplitButton、ToggleSplitButton；普通 C++ 行为、类型声明、编译模板、Gallery 共用现有树、属性、事件与合成器。
- Popup 处理期望尺寸、下方/上方放置、窗口内限制、外部按下关闭且不穿透、Escape、内容内 Tab、焦点恢复，以及锚点卸载/移除/禁用/隐藏、窗口失焦/最小化的清理。按钮族区分主动作与次动作，Toggle 主动作先更新勾选；补齐鼠标激活后的复合控件键盘焦点、F4/Alt+Down/Space。
- 继续接入 MenuFlyoutItem、ToggleMenuFlyoutItem 和 MenuFlyoutSeparator，MenuFlyout 持有实际菜单项对象；支持 C++ 命令回调和菜单键盘路径，状态通过实际依赖属性保留。示例已经换用这组实际菜单项；`--open-menu` 启动时直接创建菜单与全部模板。
- 用户调整验证策略前，按钮族相关 Debug/ASan 定向回归及 Windows 消息检查已执行；发现并修复鼠标激活后 F4 焦点问题。按随后指示停止继续回归、ASan 和截图验收，新增菜单族仅做示例编译和启动检查，完整测试留至集成末尾。保留已有 `fluent_buttons_test`，没有删除测试或改松断言。
- 最终示例目标 `velora_fluent_gallery` 编译通过，带 `--open-menu` 启动后窗口可见、菜单模板完成创建，正常关闭退出码为 0；记录为 `build/dev/menu-family-final-build.log`、`build/dev/menu-startup.json`。这不是菜单交互或完整兼容验收。
- Popup/Flyout 对象/菜单集合的完整 XAML 语法、源码类型反射继承、嵌套/窗口外弹出、完整 Placement/模板/动画、ICommand、子菜单/单选菜单、完整 UIA 等仍待实现；菜单新路径尚未经过完整行为回归。当前范围及来源差异见 `modules/fluent/PORTING.md`。

### 当前接入：Windows 图标和单选菜单

- Windows 图标按用户决定使用系统 Segoe MDL2 Assets。补齐 TextBlock/Button 的 FontFamily 绘制路径、DisplayList 字体族及 DirectWrite 缓存键；头像、群组、下拉箭头和菜单勾选/单选标记使用 MDL2，正文仍使用 Segoe UI。跨平台字体策略暂缓。
- 新增实际 FontIcon、SymbolIcon、RadioMenuFlyoutItem，接入 Fluent 构建、类型注册及 Gallery。SymbolIcon 当前只有 14 个常用 SDK 枚举值，XAML 先用整数值；菜单 IconGlyph/快捷键文字/GroupName/IsChecked 均读取实际依赖属性。同组互斥和不反选来自固定 WinUI 源码，不恢复内部影子勾选值。
- 只做示例目标编译和启动检查，不新增或执行回归、ASan、截图验收。字体外观、单选交互、分组和原有控件回归均留到最后统一验证；普通文字完整字体测量、完整 Symbol 枚举、快捷键注册、子菜单和 UIA 仍待完成。
- `velora_fluent_gallery` 最终编译通过，`--open-menu` 启动正常、窗口可见，关闭退出码 0。记录为 `build/dev/icons-menu-final-build.log`、`build/dev/icons-menu-startup.json`，不把启动检查等同于上述行为验收。

### 当前接入：MenuBar 与 AppBar 按钮族

- 新增 MenuBar、MenuBarItem、AppBarButton、AppBarToggleButton 实际 C++ 控件及编译模板。MenuBar 持有实际标题项并允许 append/clear；标题项复用 MenuFlyout，菜单栏与 AppBar 按钮均进入正式 XAML 类型目录和 Gallery。
- 已实现菜单栏点击开闭、悬停切换、左右/Home/End 导航和上下展开路径；Popup 增加仅限菜单栏祖先区域的输入透传及左右键路由。状态来自实际 Flyout，未新建影子展开值。AppBar 共用图标/标签模板、紧凑布局、Click 和先勾选后通知的 Toggle 行为。
- 示例目标编译通过，`--open-menubar` 启动时加载菜单栏及菜单内容，窗口可见，关闭退出码 0。证据为 `build/dev/menubar-appbar-build.log`、`build/dev/menubar-appbar-startup.json`。仅编译和一次启动检查，没有新增/执行回归、ASan 或截图验收。
- 上述新交互尚待统一验收；F10/Alt 访问键、完整 RTL/主题/模板状态、可观察集合与 Items XAML、AppBar 完整属性/命令、CommandBar 溢出及完整 UIA 仍待实现。详见 Fluent PORTING.md，不把启动成功计为完整控件兼容。

### 当前接入：CommandBar 与显式溢出命令

- 新增 CommandBar、AppBarSeparator 实际控件和编译模板，Gallery 包含主命令、次命令、分隔线与勾选命令。集合通过 C++ append/clear 接入，CommandBar 本体与分隔线进入正式 XAML 类型目录。
- 溢出面板复用原来的 AppBarButton/AppBarToggleButton 对象，按所在位置选择竖排/横排模板；IsChecked 保留在同一依赖属性中，IsInOverflow 为只读依赖属性。分隔线方向通过模板优先级设置，不覆盖调用方的本地属性值。新增按钮 C++ 命令回调，复用现有 Click 路径。
- 已接入 IsOpen、More 按钮、主命令左右/Home/End 导航、溢出上下/Home/End 导航及点选关闭路径；开闭和卸载使用既有 Flyout/Popup 生命周期。上述交互仍待集成末尾统一验收。
- `velora_fluent_gallery` 编译通过；`--open-commandbar` 启动时成功创建并打开次命令面板，窗口可见，正常关闭退出码 0。记录为 `build/dev/commandbar-build.log`、`build/dev/commandbar-startup.json`。本批没有新增或运行回归、ASan、截图检查。
- 此批最初只支持显式 SecondaryCommands，后续宽度驱动接入见下一节。动态溢出顺序/通知、CommandBarFlyout、Content/ClosedDisplayMode、ICommand/完整图标 API、集合 XAML、完整主题/视觉状态与 UIA 仍待实现。启动成功不代表完整 WinUI 2.3 兼容。

### 当前接入：CommandBar 按宽度自动溢出

- 新增 IsDynamicOverflowEnabled 的依赖属性、C++ 与编译 XAML API。主命令按自然宽度测量，空间不足时预留 More 按钮并收纳末尾命令，变宽后按原顺序恢复；溢出的主命令排在显式次命令之前，PrimaryCommands/SecondaryCommands 集合本身不改写。
- 元素树新增 reparent，在同一树内移动子树并保留对象、ID、订阅及模板，更新继承父级和布局失效；adopt 仍限定独立根。关闭时，收纳项保留在折叠容器，测量使用主区域模板；打开时原对象加载溢出模板，关闭后下一次布局恢复。不创建菜单代理或第二份勾选状态。
- 新增 invalidateMeasure 接口，校验元素存在及所属线程。CommandBar 的有效值通知显式触发布局失效，继续遵守 `.vtypes` 当前 Paint 属性契约；首次启动暴露的属性契约不匹配已据此修正，示例增加异常错误输出。
- 只读 IsInOverflow 反映 AppBar 按钮是否被收纳；主区键盘导航排除收纳项。收纳聚焦命令时尝试将焦点交给 More，展开时宽度或动态溢出配置改变会先关闭面板再布局。
- `velora_fluent_gallery` 最终编译通过；`--open-commandbar` 启动确认末尾主命令已经自动溢出，随后成功打开包含主/次命令的面板，窗口可见并正常关闭，退出码 0。证据为 `build/dev/dynamic-overflow-final-build.log`、`build/dev/dynamic-overflow-startup.json`。改动文件格式、CheckStyle、CheckWorkspace 与 diff 检查通过；没有新增/运行回归、ASan 或截图验收。
- 此批尚未实现 DynamicOverflowOrder 分组/变化通知，后续接入见下一节；分隔线合并、RTL、展开期间原位重排，以及完整重挂载、绑定继承、焦点、卸载与视觉状态仍待统一验收。

### 当前接入：溢出优先级分组与 C++ 通知

- AppBarButton、AppBarToggleButton、AppBarSeparator 新增 DynamicOverflowOrder 的真实依赖属性、类型化 C++ API 及编译 XAML 声明；当前接受非负 32 位整数，其余类型/范围由对象校验拒绝，各写入路径共用同一属性。
- 正数按升序整组收纳，同一正数优先级的命令一起移入/恢复；0 保留从末尾逐项收纳的默认方式。布局记录改为逐项位置，主区键盘导航和溢出面板按原始集合顺序过滤，不再假定可见项必须是前缀，也不改写两个命令集合。
- 新增 addDynamicOverflowItemsChanging/removeDynamicOverflowItemsChanging：移动前同步通知加入/退出方向及本批实际命令对象，支持 token 退订及分发中退订。回调用于观察计划；集合修改及回调中打开面板被拒绝，布局属性的回调内反复修改尚未完成语义。不是 XAML 事件连接或完整 WinUI 事件兼容。
- 命令有效优先级变化时关闭现有面板并请求重新测量；属性监听使用弱引用，随清空/卸载解除。追加失败也会清理新增订阅和已挂载项。
- Gallery 将前部 Save、分隔线和 Pin 设为同组，展示 C++ 通知，并通过编译 XAML 设置独立 AppBarButton 的优先级。示例目标编译通过；启动确认前部按钮成组溢出且末尾命令仍在主区，随后打开面板，窗口正常关闭，退出码 0。日志为 `build/dev/overflow-priority-build.log`、`build/dev/overflow-priority-startup.json`。未新增/运行回归、ASan 或截图验收。
- 不把启动覆盖的分组路径视为恢复、动态改优先级、通知重入/解绑与全部生命周期验收；完整 CommandBar 属性、集合 XAML、分隔线整理、主题/RTL/UIA 仍待补齐。

### 当前接入：SplitView 与 TwoPaneView

- 新增两个实际 Fluent 控件、正式类型声明和编译模板。PaneLayout 共用内容挂载、卸载、测量、排列和裁剪；SplitView.Content 沿用底座 Content 属性，Pane/Pane1/Pane2 使用持有原 UIElement 的对象依赖属性，无第二份内容字段。对象内容通过 C++ 接入，嵌套对象 XAML 尚不支持。
- SplitView 接通 IsPaneOpen、OpenPaneLength、CompactPaneLength、DisplayMode、PanePlacement；实现四种显示模式、左右侧栏、内容挤占/覆盖、实际遮罩、轻关闭与 Escape 路径。TwoPaneView 保留上游单显示区域的严格阈值及先横排后竖排规则，接通优先级、正向/反向排列、Auto/像素/星号长度、只读 Mode 和 C++ ModeChanged。Mode 在实际排列后发布，避免测量阶段发出临时模式通知。
- 内容拒绝跨线程、重复挂载、外部已挂载对象和 PaneLayout 间的强引用环；挂载时拒绝重入内容写入。隐藏槽清除其焦点/指针捕获，卸载保留属性中的内容对象以便重新挂载。复杂回调中 clearValue、失败后属性恢复和完整 UI 线程销毁仍待统一验证。
- 示例编译通过；`--preview-panes` 启动确认四个内容对象已挂载，TwoPaneView 为 Wide、SplitView 已展开；窗口可见并正常关闭，退出码 0。证据为 `build/dev/pane-layout-final-build.log`、`build/dev/pane-layout-startup.json`。没有新增/执行回归、ASan、截图验收。
- 完整焦点约束、开闭事件/取消、动画、主题/RTL/UIA、多显示区域/铰链、原 ScrollViewer 模板及全部模式切换仍待补齐或统一验收。TwoPaneView 长度属性暂为单个 GridLength 的字符串语法，不是完整 GridLength 元数据 API。

### 后续推进：RatingControl 与 ToolTip（2026-11-11）

- RatingControl 进入正式 Fluent 类型声明、编译 XAML 与 Gallery。七个实际依赖属性提供类型化 C++ API，接通范围修正、小数评分裁剪、占位评分、标题、只读/清除、指针预览/捕获和方向键/Home/End；用户操作提供可退订 ValueChanged。星形使用系统 Segoe MDL2 Assets。
- ToolTip 提供共享持有的 C++ attach/detach/show/hide 与实际 Content 字符串属性。复用 Flyout 和窗口弹出层，增加不抢焦点、不拦截命中、不限制 Tab 范围的提示模式；按键、按下和滚轮关闭提示后继续正常输入，锚点卸载解除订阅。已有菜单打开时提示不替换菜单。
- 当前提示为立即悬停显示，尚无延迟/自动消失计时、键盘焦点触发、ToolTipService 附加 XAML 或完整定位 API。评分采用固定调色板，MaxRating 上限暂为 1000；ItemInfo、动画、RTL、完整主题/高对比度/UIA 和复杂重入生命周期待补齐或统一验收。ToolTip 仅是 C++ 服务，不计作正式 XAML 控件类型。
- 首次简单启动发现评分代码将颜色字符串直接写入画刷属性，导致绘制读取整数失败；已改为底座需要的 ARGB 整数。接入期只编译和简单原生启动/关闭，不新增或运行回归、ASan、截图验收。
- 修复后 `velora_fluent_gallery` Windows Debug 构建通过；`--preview-rating` 确认 3.5 分初始值和提示显示后焦点保留，窗口可见、正常关闭、退出码 0。证据为 `build/dev/rating-tooltip-fixed-build.log`、`build/dev/rating-tooltip-startup.json`；输入、主题、生命周期的完整验收仍未执行。

### 后续推进：CommandBarFlyout（2026-11-11）

最新接入：CommandBarFlyout 已内化为 Fluent C++ 服务和编译呈现模板，主命令横排、次命令纵排，直接挂载实际 AppBarButton/AppBarToggleButton/AppBarSeparator。Standard 打开时展开次命令；More 收起/展开，方向键与 Home/End 导航，次命令 Click 关闭；Escape、外部点击与焦点恢复沿用 Popup。集合编辑暂要求关闭后进行，关闭解除事件订阅，保留命令对象供重开使用。

本批 Windows Debug Gallery 构建通过；`--open-commandflyout` 确认主/次命令实际挂载，原生窗口可见并正常关闭、退出码 0。证据为 `build/dev/command-flyout-final-build.log`、`build/dev/command-flyout-startup.json`。未新增/执行回归、ASan 或截图验收。首轮示例变量重名导致编译失败，修正后通过；首次日志保留在 `build/dev/command-flyout-build.log`。

尚未实现 Transient/选择浮窗模式、TextCommandBarFlyout、可观察集合和集合 XAML、展开动画/阴影、RTL、完整 UIA、窗口外/嵌套弹出及完整键盘/生命周期验收。当前主命令不自动溢出，宽度受窗口约束；次命令关闭接在 Click，尚未覆盖程序设置 Checked/Unchecked 的上游关闭契约。该服务不增加正式 XAML 类型数。

### 后续推进：ComboBox 与 RadioButtons（2026-11-11）

本批新增 ComboBox 和 RadioButtons 两个正式 XAML 类型，共用 ChoiceControl 的字符串项集合、SelectedIndex 实际依赖属性、Header、类型化访问和可退订 SelectionChanged。SelectedItem 当前是从索引派生的只读字符串 getter，不额外保存选择副本；ContainerFromIndex 返回当前已实现的实际容器。

ComboBox 接通 PlaceholderText、IsDropDownOpen，复用 MenuFlyout/Popup；点击或 F4/Alt+Down/Space/Enter 打开，初始焦点定位到已选项，选择后关闭，Escape 取消沿用弹出层。RadioButtons 使用实际 RadioButton 和独立组名，MaxColumns 控制列优先、余项分配；SelectedIndex 更新勾选和组内 Tab 停靠项，上下按项导航、左右按相邻列同行导航，Ctrl 保留选择只移动焦点。

当前只支持 C++ setItems(vector<string>)，集合替换保留有效索引、越界清为 -1。XAML 在集合到达前设置的索引暂保留，设置项后校验。完整 ItemsSource/通知集合、Items 属性元素、ItemTemplate、可写 SelectedItem、可编辑 ComboBox/搜索、长列表滚动/虚拟化、完整主题/UIA/游戏手柄及重入生命周期未完成；RadioButtons 的底座 Grid 尚非上游按最大子项统一尺寸的布局器。

本批 Windows Debug Gallery 构建通过；`--preview-choices` 确认 ComboBox 打开、初始选择 Light，以及 RadioButtons 初始选择 Compact，窗口可见并关闭、退出码 0。证据为 `build/dev/choices-final-build.log`、`build/dev/choices-startup.json`。未新增或运行回归、ASan、截图验收，键盘/指针/绑定及生命周期仍待统一验证。

### 后续推进：ContentDialog 与 TeachingTip（2026-11-11）

本批继续接入 ContentDialog 与 TeachingTip 两个 C++ 弹出服务，共用 PopupPanel 的编译呈现模板、实际字符串/布尔依赖属性、按钮事件、可取消 Closing、Closed 与 UI 线程目标生命周期。服务类当前不注册正式 XAML 类型；模板仍正常编译打包，不使用运行期文本 XAML。

- ContentDialog 居中显示，外部点击不关闭，弹出层限制输入和 Tab；支持标题/字符串正文、主/次/关闭按钮文字、主/次按钮启用状态、默认按钮焦点、None/Primary/Secondary 结果。showAt 是同步打开入口，尚无 ShowAsync/deferral 契约。
- TeachingTip 通过 attach 关联目标，IsOpen 驱动实际弹出层；支持标题、副标题、字符串正文、动作/关闭按钮和 IsLightDismissEnabled。动作按钮通知后保持打开，关闭按钮走关闭流程。无轻关闭时外部控件仍可命中和获得焦点；轻关闭开关支持打开期间更新。
- 两者提供带 token 的按钮/关闭前/关闭后订阅及退订；按钮与关闭前回调可取消普通关闭。锚点移除解除目标并强制关闭；宿主销毁或其他弹出层替换不受取消限制。沿用单窗口单弹出层、窗口内定位与既有 Flyout 外框资源。
- 当前只支持 C++ 服务和字符串内容；完整对象内容/XAML 元数据、TeachingTip 尾巴/全部 Placement/HeroContent/IconSource、主题动画/阴影/模态遮罩、异步 deferral、UIA 和完整生命周期验收仍缺失。共享模板宽度暂为 360，尚无完整响应式和换行内容布局。此批不能标为完整迁移。

Windows Debug Gallery 构建通过；`--open-dialog` 与 `--open-teachingtip` 各自确认实际打开，两个原生窗口均可见并正常关闭、退出码 0。证据为 `build/dev/popup-panels-fixed-build.log`、`build/dev/dialog-startup.json`、`build/dev/teaching-tip-startup.json`。首轮模板使用尚不支持的 MaxWidth，后续 C++ 缺少显式 ElementFactory 头，均已修复，失败日志保留。未新增或执行回归、ASan、截图验收；取消、结果、焦点和生命周期的完整交互尚未验证。

### 后续推进：Pivot 与 FlipView（2026-11-11）

本批接入 Pivot 与 FlipView 两个正式 XAML 类型，共用 PageSelector 和编译模板。setPages 接受字符串页头及原 UIElement 内容；SelectedIndex 为实际依赖属性，提供派生 SelectedItem 和可退订 SelectionChanged。首次设置非空集合时保留有效索引，否则选择首项；显式 -1 可清空显示，空集合显示空页。

- Pivot 支持字符串 Title、IsLocked、真实 Button 页头、点击/左右/Home/End 与 Ctrl+Tab/Shift+Ctrl+Tab 切换；锁定限制用户导航，程序设置 SelectedIndex 仍可用。
- FlipView 使用系统 Segoe MDL2 Assets 的前后按钮，边界禁用；控件获得焦点后支持方向/Home/End。内容中的编辑控件正常接收自己的方向键，导航没有强占任意内容按键。
- 所有页面对象挂载到同一树内，非选中页面通过独立宿主折叠，不修改页面本身 Visibility；切换保留控件状态并清理隐藏页面的焦点/捕获。集合替换会重新挂载内容；虚拟化、缓存策略、动画/滑动/触摸/RTL、页头溢出滚动、完整容器类型/主题/UIA 尚未实现。
- UIElement 新增 ownedContentObjects/containsOwnedContent，枚举内容属性/集合持有的实际对象。PaneLayout 与 PageSelector 共用路径检查，拒绝自引用、跨容器内容环、重复项和外部已挂载内容。第三方内容容器须实现该所有权枚举；复杂回调重入、失败恢复与 UI 线程销毁继续待统一验证。
- 页面集合首版仅 C++ setPages，尚无 ItemsSource/通知集合、对象集合 XAML、完整 PivotItem/FlipViewItem 类型和可写 SelectedItem 元数据。原 WinUI 输入在这两个目录中只有系统模板，当前逻辑由 Velora 实现，不能声称完整移植了上游系统 C++。

Windows Debug Gallery 构建通过；`--preview-pages` 确认 Pivot 初选 Notes 编辑页、FlipView 首项及实际树内挂载，原生窗口可见并正常关闭、退出码 0。证据为 `build/dev/page-selectors-final-build.log`、`build/dev/page-selectors-startup.json`。首轮使用不存在的 ToggleButton 元素枚举导致编译失败，已改用实际 Button 页头；原日志保留。未新增或执行回归、ASan、截图验收，切换、状态保持、内容环与销毁的完整行为仍待统一验证。

### 后续推进：TabView 与 TabViewItem（2026-11-11）

本批接入 TabView 和 TabViewItem 两个正式类型，共用 PageSelector 的编译呈现、SelectedIndex 和实际内容挂载。TabViewItem 的 Header/IsClosable 使用实际依赖属性，Content 沿用底座属性保存原 UIElement 或字符串；未建立第二份标签状态。TabView 的集合由 PageSelector 持有原 TabViewItem，页签栏从项属性派生呈现。

- 接通 IsAddTabButtonVisible、MDL2 添加/关闭按钮、Ctrl+Tab/Shift+Ctrl+Tab 切换与 Ctrl+F4 关闭请求。添加按钮只通知应用，关闭先通知 TabView 再通知 TabViewItem；运行时不自动删除。IsClosable=false 隐藏关闭按钮并拒绝关闭请求。
- setTabItems/removeTab 在重建集合时按实际项保留选择，选中项移除后选择附近项；尝试按原焦点对象恢复焦点，失效则回到选中页签/添加按钮。Header/IsClosable 变化更新页签栏，重建或卸载解除项监听和事件订阅。标签内容参与通用所有权环检查，非选中页面折叠保留对象。
- 示例包含不可关闭的欢迎项、实际编辑页及应用处理的添加/关闭回调。当前仅 C++ 集合 API；通知集合/TabItemsSource、对象集合 XAML、HeaderTemplate/IconSource、TabWidthMode、拖拽/重排、滚动溢出、鼠标中键、完整主题/动画/UIA 尚未实现。事件参数和选择通知仍是 Velora 首版 API，不能标为完整 WinUI 兼容。
- 集合编辑仍采用重新挂载，完整回调重入、焦点/编辑状态恢复和失败事务留到统一验收。TabViewItem 的 Header/IsClosable 有正式 XAML 声明，实际对象 Content 当前经 C++ 连接。

Windows Debug Gallery 构建通过；`--preview-tabs` 确认初选 Document 标签、原编辑器实际挂载和欢迎项不可关闭，原生窗口可见并正常关闭、退出码 0。证据为 `build/dev/tabs-final-build.log`、`build/dev/tabs-startup.json`。CheckStyle、CheckWorkspace、改动文件格式及 diff 检查通过；未新增或执行回归、ASan、截图验收，增删、请求顺序、选择和焦点完整交互仍待统一验证。

### 后续推进：AutoSuggestBox（2026-11-11）

本批接入正式 AutoSuggestBox 类型和两个编译模板，复用实际 TextBox、窗口内 Flyout、按钮与 ScrollViewer。Text/Header/PlaceholderText/IsSuggestionListOpen/UpdateTextOnSelect 是实际依赖属性；首版以 QueryIconGlyph 字符串接入 Windows Segoe MDL2 查询图标，尚非完整 QueryIcon 对象 API。

- C++ setSuggestions 接收 UTF-8 字符串列表，支持输入时替换建议并重新打开呈现器。TextChanged 区分 UserInput/ProgrammaticChange/SuggestionChosen；SuggestionChosen 和 QuerySubmitted 发布选中字符串/查询快照并支持 token 退订。点击或 Enter 可提交，上下键预选、Escape 关闭、Tab 关闭后正常导航；已有组合文本时不接管建议导航键。
- PopupOptions 增加 takeFocus，建议列表交互命中与保留编辑焦点可同时使用；非模态轻关闭保留指定锚点的命中穿透，输入框和查询按钮不因位于弹出区域外而先被关闭流程吞掉。建议列表滚动、外部点击关闭与卸载退订复用既有服务。
- 示例在 UserInput 通知中同步过滤一组字符串，并报告查询；框架不内置应用搜索策略。首版建议视口上限固定 200 DIP，全部生成按钮，无虚拟化和键盘预选自动滚入。完整 ItemsSource/对象项/ItemTemplate/TextMemberPath、异步数据源取消、标准 QueryIcon、完整主题/圆角联动/UIA、焦点离开策略和实际 IME 交互仍待完善。
- 这些输入主要是系统控件模板及圆角 Helper；未将 Helper 当作完整 AutoSuggestBox 系统 C++。当前行为由 Velora 实现，事件参数与导航/提交契约仍待最后统一验收。

Windows Debug Gallery 构建通过；`--preview-suggestions` 确认三条建议、建议列表打开和编辑器焦点保留，原生窗口可见并正常关闭、退出码 0。证据为 `build/dev/auto-suggest-final-build.log`、`build/dev/auto-suggest-startup.json`。源码布局、工作区、改动文件格式和 diff 检查通过；未新增或执行回归、ASan、截图验收，输入、更新/提交、IME、跨控件弹出与生命周期完整交互仍待统一验证。

### 控件接入数量快照（2026-11-11）

以 StageFluentSources.py 的 49 个源码区域为范围，扣除 Collections、Common、CommonStyles、Effects、IconSource、inc、Interactions、LayoutPanel、Lights、Materials、ResourceHelper 这 11 个支撑区域，跟踪 38 个功能组。此口径不是最终 WinUI 2.3 正式公开类型清单；一个功能组可能包含多个控件，experimental 条件仍需审计。额外系统控件及 Button/TextBox 等通用底座不能用此数推算完整总数。

| 接入状态 | 功能组数 | 范围 |
| --- | --- | --- |
| Fluent 部分接入 | 20 | AutoSuggestBox、ComboBox、CommandBarFlyout、ContentDialog、DropDownButton、FlipView、MenuBar、MenuFlyout、NumberBox、PersonPicture、Pivot、RadioButtons、RadioMenuFlyoutItem、RatingControl、SplitButton、SplitView、TabView、TeachingTip、ToolTip、TwoPaneView |
| 通用底座部分实现 | 4 | CheckBox、ProgressBar、ScrollViewer、Slider |
| 尚未接入运行时 | 14 | 下列清单 |
| 完整 WinUI 2.3 兼容验收通过 | 0 | 当前接入期未进行最终统一验收 |

剩余 14 组：AnimatedVisualPlayer、CalendarDatePicker、CalendarView、ColorPicker、DatePicker、NavigationView、ParallaxView、PullToRefresh、Repeater、ScrollBar、Scroller、SwipeControl、TimePicker、TreeView。此数不包含已接入组内部尚缺的 API、模板、主题、输入与辅助功能。

Fluent 正式 XAML 控件类型现为 27，另有 Flyout/MenuFlyout/ToolTip/CommandBarFlyout/ContentDialog/TeachingTip 等 C++ 服务。完整按类型的历史 Gallery/API 清单仍待固定，不能将“14 个功能组”写成“仅剩 14 个控件”。

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
