# Velora 总体设计

本文件描述目标架构。标为“现状”的内容来自当前代码；其余是待实现约束，不代表功能已经交付。插件与工具链分别见 [PLUGIN-SDK.md](PLUGIN-SDK.md)、[XAML-TOOLCHAIN.md](XAML-TOOLCHAIN.md)。

## 1. 产品边界

Velora 面向新应用，使用 C++23 和编译式 XAML，保留属性、绑定、模板、资源、事件和代码后台的 WPF/UWP 开发体验。视觉与交互对齐 WinUI 2.3；不承诺 WPF/UWP 源码、WinRT 组件或微软二进制资源格式兼容。

首批平台为 Windows 与 Android。Windows 使用 Win32、Direct2D、DirectWrite；Android 使用 Impeller 渲染后端。框架自行负责控件、布局、输入语义和合成树。跨平台服务先覆盖 UI 所需的窗口/表面、调度、输入、IME、剪贴板、字体、资源及辅助功能，不扩张为通用操作系统 API 重实现。

最终验收必须完整覆盖锁定的 WinUI 2.3 历史 Gallery 清单。先固定参考版本、页面、控件、状态、交互与资源来源，再逐项记录实现和证据；现有同名类、截图或空处理器均不构成完成。Android 使用相同控件语义，并落实触摸、软键盘和移动端生命周期。

## 2. 本轮基线与待实现内容

现状：保留 DependencyObject/DependencyProperty、元素树、Measure/Arrange、绑定、模板、Composition、Windows 后端，以及 `.vxbf/.vpri/.vpak` 管线。移除旧稳定对象 ABI 的双指针包装、接口查询、引用计数协议、逐控件投影、激活桥和生成器。TypeId 留作类型声明身份，不再代表组件接口身份。

待实现：Android 后端、完整 Gallery、开放控件元数据、源码裁剪、二进制插件 SDK、编辑器预览，以及统一的 UI 线程销毁策略。清理成功只说明旧包装退出，不能替代上述验收。

## 3. 模块职责

| 模块 | 职责与依赖方向 |
| --- | --- |
| foundation | 基础类型、容器、内存、编码、类型身份；不依赖 UI |
| reflect | 普通 C++ 类型、属性、事件元数据与访问器 |
| runtime | 对象和依赖属性语义、类型注册；不引入窗口或绘图后端 |
| resources | URI、资源索引、限定条件选择与包读取 |
| xaml | 二进制 IR、校验、抽象加载目标；不直接依赖具体控件 |
| controls | 元素树、布局、事件、控件行为、模板与 loader 适配 |
| graphics | 绘图与文本服务接口；当前 text 与 graphics 同一物理库 |
| composition | 保留式视觉树、无效化、绘制快照、帧调度 |
| platform/application | 原生窗口与输入、UI 调度器、应用/窗口/XamlRoot 生命周期 |
| fluent | WinUI 2.3 内化后的实际控件 C++、模板与主题资源；依赖 controls 通用底座 |
| markup | 仅构建期使用的反射/XAML 编译器 |

一个逻辑模块可由多个源文件或内部目标组成，不要求每模块必有一个 DLL。旧 Framework/ElementTree 的调度职责应逐步收敛，保持工作路径，不另造并行运行时。

## 4. 对象、属性与线程

每个控件只有一个实际 C++ 对象、一份依赖属性有效值存储和一个生命周期。普通 virtual 方法承担布局、输入、绘制扩展，不生成同名外部代理。继承、模板、绑定都读写同一对象的属性，禁止插件维护第二份 Content/Value 等影子状态。

过渡期沿用树持有 `shared_ptr` 的路线。父关联与订阅回指使用弱引用或非拥有树标识，回调捕获弱引用；页面卸载负责解除订阅和取消异步工作。逻辑父与模板视觉父分别表达。禁止同时引入父对象直接 delete 子对象的另一套所有权规则。

UI 对象的创建、修改与实际析构在所属 UI 线程执行。异步工作默认持有弱引用或树标识，回到 UI 线程后再使用对象。需要支持工作线程释放最后引用时，由统一工厂的轻量 deleter 投递到 UI 调度器；这只是线程调度，不重建旧对象分配器、页面域或外部引用计数协议。关闭前停止任务、解除绑定、卸载树并排空待销毁对象。残留强引用属于生命周期错误，应诊断并修正持有方，不能强行跨线程析构或提前卸载插件。此规则需先于插件交付验证。

### 当前普通 C++ API 切片

类型化工厂与 Toggle 属性已接入同一实际对象，示例（包含 `BuiltinElements.hpp` 和 `ElementFactory.hpp`）：

```cpp
auto toggle = velora::createElement<velora::ToggleSwitchElement>();
toggle->setIsOn(true); // 可以先在树外配置
const auto id = ui.insert(toggle, parent);
ui.on(id, ui.events().Toggled, [] { /* 响应有效值变化 */ });
toggle->setIsOn(false); // 挂载后触发宿主事件及 TwoWay 写回
```

`isOnProperty()` 可用于既有依赖属性层级 API；CheckBox/RadioButton 对应 `ToggleButtonElement::isChecked/setIsChecked/isCheckedProperty`。工厂应用与 XAML 相同的默认值，不创建代理或第二套状态。目前事件仍通过 Framework 注册，三态 CheckBox、普通控件完整继承层次仍待实现。基本 virtual 输入/绘制入口见下节。

`SliderElement` 与 `ProgressBarElement` 继承 `RangeBaseElement`，公开 `minimum/maximum/value`、对应 setter 和静态属性描述符；Slider 另有 `stepFrequency/setStepFrequency`。范围校验在对象中执行，覆盖普通属性写入、表达式发布和清除属性后的回退。依赖属性提供受保护的校验与值规范化扩展点，在一批通知前调整关联值；不会创建第二份属性存储，也不会因截断值而解除表达式。

当前范围行为保留原实现的破坏性截断：Maximum 至少为 Minimum，Value 位于两者之间，非有限值和负步长被拒绝；放宽范围不会自动恢复之前被截断的原始值。这不是完整 WinUI coercion 语义的完成声明，仍须对照参考环境补齐。绑定源发布越界值时只截断目标，不递归改写源；之后的用户写入及范围变化继续使用 TwoWay 写回。

### 事件订阅生命周期

`Framework::subscribeEvent(element, event, handler, handledEventsToo)` 返回可移动、不可复制的 `EventSubscription`。调用 `reset()`、离开作用域或移动赋值覆盖旧订阅时解除对应注册，不替换同一事件的其他处理器。处理器接收 sender 和 `RoutedEventArgs`，默认跳过已处理事件；观察已处理事件需显式传入 `true`。既有 `on()` 仍是替换式接口，且其回调会将事件标为已处理。

```cpp
auto subscription = ui.subscribeEvent(id, ui.events().Toggled,
    [](velora::ElementId sender, velora::RoutedEventArgs& args)
    {
        // 响应事件；需要停止冒泡时设置 args.handled。
    }, true);
// subscription 必须保留到需要停止观察时，通常作为页面或控件成员。
```

订阅不持有控件或宿主。移除元素会释放处理器及其捕获对象；订阅句柄可晚于元素和宿主销毁。宿主存活时，订阅的移动、reset 和析构遵循 UI 线程亲和性，跨线程解除订阅会 fail-fast；当前未实现自动投递销毁。此接口仍在 Framework 上，控件对象事件访问器继续待实现。

### 源码控件的输入与绘制

`UIElement` 提供 `onPointerPressed/onPointerMoved/onPointerReleased/onKeyDown`，沿既有路由在当前 sender 的实例处理器前调用。输入参数是 `PointerEventArgs` 或 `KeyEventArgs`；实例订阅可通过 `dynamic_cast` 读取对应参数。指针 x/y 按当前 sender 的局部坐标更新，windowX/windowY 保留窗口 DIP 坐标；键盘参数包含 Key、Shift、Ctrl。设置 handled 消费自定义输入，默认实例订阅跳过，handledEventsToo 仍可观察。内建控件的现有宿主行为继续作为过渡期实现，尚未整体迁入控件类。

短期借用的 `InputContext` 提供当前元素的 bounds、focus、capturePointer/hasPointerCapture/releasePointer，不得保存到回调之外。调用 virtual 前宿主持有实际对象，允许回调移除自身及释放调用方引用；已移除源不再继续实例分发。指针捕获只在指针入口选择目标，键盘、焦点及控件状态事件按各自来源路由。自定义类覆盖 `isFocusable()` 后可参与 Tab 导航，仍受 IsTabStop、可见性和祖先启用状态约束。

`renderOverride(Canvas&, Size) const` 在已有背景之后、子视觉之前记录局部绘图命令，返回 true 接管内容绘制，false 使用注册的默认 painter。属性有效值变化通过现有 VisualBridge 触发重绘，快照仍为不可变 DisplayList。渲染回调不得改变树或属性；本轮不提供额外 overlay、文本编辑、触摸多指或指针取消 API。

`examples/custom_control/ValuePad` 是实际源码示例，使用同一依赖属性保存数值，支持拖动、键盘、Tab 和虚拟绘制。其类名与 TypeId 现在为声明中的 `Demo.ValuePad`，布局 kind 继续沿用内建 UserControl 基类作为过渡。`SourcePage.xaml`、同一份 `.vtypes` 声明及普通 C++ 工厂已贯通正式编译加载，当前限叶节点、字面值与 x:Name；完整扩展语义及插件仍是 2A 待办，详见 XAML-TOOLCHAIN.md。

## 5. 后端与发布

Android 后端适配绘图命令和文本测量，平台层另行接入 surface 重建、触摸、IME、前后台及屏幕参数变化。首先打通窗口、文本、输入和资源回收，再验收控件。Impeller standalone 可被非 Flutter 应用使用，但目前不承诺 API 稳定；必须锁定上游提交和构建参数，通过 tpkg 管理。[官方说明](https://github.com/flutter/flutter/blob/master/engine/src/flutter/impeller/toolkit/interop/README.md)

| 发布配置 | 产物与约束 |
| --- | --- |
| 静态自包含 | Windows 可执行文件嵌入框架和资源；选择的源码控件静态注册，可裁剪；新增原生代码需要重编 |
| 应用本地共享运行时 | 应用目录携带匹配的运行时、插件及资源；宿主与插件共享同一运行时和注册表；不要求系统安装 Velora |

Android 对应自包含应用包，原生库按目标架构打包。二进制插件不能各自静态链接出第二套运行时。裁剪必须覆盖类型注册、默认模板和资源引用的依赖闭包，不能只依赖链接器碰巧丢弃未使用符号。

## 6. 性能与验收

性能目标是响应、启动、内存、包体与功耗的均衡，不预设任意小 MB 指标。Windows 使用相同内容、控件数量、图片、字体、DPI、主题、滚动与动画负载，与 UWP 参考程序对照；分别记录冷/热启动、空闲与峰值内存、帧耗时分布、CPU/GPU 和包体。Android 在固定真机与负载上建立自身基线，不将不同系统的绝对内存直接相减。

优先解决测量重复、无效化过宽、列表非虚拟化、资源重复解压及无界缓存。每次优化附同条件前后数据与功能回归。Gallery 验收同时覆盖显示、输入、键盘/焦点、主题、DPI、文本、辅助功能和销毁；成功构建或单一截图不能代替这些结果。
