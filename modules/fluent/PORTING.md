# Fluent 内部控件

WinUI 2.3 的逻辑、模板和资源直接改写为本模块的普通 C++ 控件及编译 XAML。`controls` 提供树、属性、布局、输入、模板和渲染底座；本模块拥有控件行为。不会构建微软上游运行时或保留 WinRT 投影。

来源固定为 microsoft/microsoft-ui-xaml 的 `v2.3.191129002`、提交 `2237b09d9ba6052414ddccf3d61316de20564303`。原文件哈希见 [WinUI23.provenance.json](WinUI23.provenance.json)，完整 MIT 文本见 [LICENSE-WinUI2.3.txt](LICENSE-WinUI2.3.txt)。许可证同时安装到 SDK 并包含在 Fluent 资源包中。

完整上游镜像已从受版本管理的源码树移除。本机参考缓存位于忽略的 `build/reference/winui2.3/`，不是依赖或交付物；干净工作区可按来源链接取得具体参考文件，不必重新把整仓加入项目。

## 批量引入，逐族内化

按新的批量引入要求，`porting/` 保存此提交中全部 49 个控件/支持源码目录的待改写输入，另包含公共 IDL 和紧凑密度样式，共 1243 个文件。这里的“目录”包含公共算法、材料、布局和资源，并非 49 个已实现控件。引入 C++、头文件、IDL、模板、主题、原有语言资源及所需图片；不引入上游工程、激活运行时、生成投影、测试应用或设计工具。

这些文件将逐族改写为 Velora 实现，不通过伪造 `winrt::` 命名空间或实现通用 WinRT/XAML 兼容层来运行原仓。`porting/` 不进入 CMake 源列表、公开头安装、XAML 编译或 VPAK；当前正式 PersonPicture 位于 `src/` 和 `resources/`，保留同名的待移植输入不产生第二个运行时对象或类型注册。

来源与阶段清单在 [porting/Sources.json](porting/Sources.json)：每个文件记录原路径、原始 SHA-256、导入后 SHA-256，每个目录记录 `source-staged` 状态。只规范 UTF-8/LF、行尾空白、文件末空行及 XAML 混合缩进；`ItemsRepeater.common.cpp` 的文件名改为 `ItemsRepeaterCommon.cpp`。原文件中的版权和注释保留，全部输入受模块 MIT 许可证覆盖。

校验已入库输入不需要参考缓存：

```powershell
python scripts/StageFluentSources.py --check
```

初次导入使用 `python scripts/StageFluentSources.py --reference build/reference/winui2.3`，逐文件核对固定提交的原始哈希后写入。脚本拒绝覆盖修改过的输入。实际适配在 `src/velora/fluent/`、`resources/` 进行，遵循 Velora 格式和 API 规则；适配完成后的输入可在对应迁移提交中删除，并同步来源清单。不能为通过构建而给未适配控件注册空工厂或把它们映射成无行为的基础控件。

### 底座替换范围

| 上游依赖 | Velora 对应能力和当前缺口 |
| --- | --- |
| WinRT 对象、生成 `.g.h`/`.properties`、装箱值 | 普通 C++ 类、现有 DependencyObject 和类型声明；逐类手写实际属性/事件，不生成 COM 投影 |
| DependencyProperty、属性回调、Binding | 现有有效值存储与绑定；补齐元数据、coercion 和源码控件表达式语义，禁止另一套属性表 |
| FrameworkElement、Control、模板部件、VisualState | 现有 UIElement/Framework、模板名称域和状态系统；完善继承、模板替换、内容属性及完整 setter/动画语义 |
| Dispatcher、Loaded/Unloaded、事件 revoker | 现有 Windows 调度、挂载/卸载和 RAII 订阅；补齐异步取消、UI 线程销毁约束 |
| Pointer/Key、焦点、Popup/Flyout、拖放 | 现有输入/捕获/焦点；弹出层、完整触摸/拖放和菜单交互仍需实现 |
| Observable collections、Repeater、ScrollViewer | 保留控件算法，接入 Velora 树和布局；集合通知、容器回收、虚拟化及完整滚动协议待补齐 |
| Composition、Brush、几何、文字与字体 | 现有合成、D2D/DirectWrite；补齐动画、效果、字体 API，不把效果缺失静默算作兼容 |
| ResourceLoader、ImageSource、Contact、UIA | 现有资源 URI/包和图片管线；本地化、联系人服务与 Windows UIA provider 待实现 |

模板输入中的 CalendarView、DatePicker、Slider、ContentDialog 等目录没有完整 C++ 控件逻辑；上游依赖 Windows 系统控件。复制目录不能取得这些实现，须由 Velora 实现行为并对照模板/API 验收。`Contracts/Microsoft.UI.Xaml.idl` 中的 experimental/版本条件也要逐项审查，固定提交中的全部源码不能直接算作 2.3 正式公开 API。

按共享底座与控件族批量接通：NumberBox、弹出按钮族之后推进菜单，再处理集合/虚拟化及导航。按用户最新要求，开发期只编译和启动检查，最后统一回归；未执行的行为验证继续列为缺口，已导入输入不计为可用控件。

## PersonPicture 首个实现切片

实际实现位于 `src/velora/fluent/PersonPicture.{hpp,cpp}`、`InitialsGenerator.{hpp,cpp}`；模板为 `resources/Controls/PersonPictureView.xaml`，主题为 `resources/WinUI2.3/PersonPictureResources.xaml`。它们进入 `velora::fluent` 与 `Velora.Fluent.vpak` 正常构建。

已内化的上游逻辑：

- DisplayName 缩写生成：字符区段优先级、尾部括号限定词、首末词、组合音标、25 次分词上限。修复有界字符串越界读取，补充正确的补充平面字符分类；Windows 大写转换使用 invariant 映射。
- 显式 Initials 优先于 DisplayName；Group、Photo、Initials、NoPhotoOrInitials 的状态优先级与恢复。
- 徽章图片优先，非零数字其次、字形最后；负数字隐藏而不退回字形，超过 99 显示 `99+`，零数字退回字形。
- 字号为直径的 42%，徽章直径 50%、字号为徽章直径 60%，字体至少 1 DIP。圆形显示区域取可用宽高较小值。
- Person/Group 名称和 BadgeText/数量组合的 en-US 辅助功能名称；内部 automation 树使用 Text 类型并隐藏模板部件。

这些行为读取同一份依赖属性，更新实际模板部件，没有平行对象或影子属性。类型声明在 `resources/FluentControls.vtypes`，`PersonPicture::registerType` 将其绑定到本模块的类；应用仍可直接用 `createElement<PersonPicture>()` 创建。挂载前需用 `mountFluent` 提供匹配当前 SDK 的编译资源包。逻辑属性可在树外使用；缺失模板时挂载明确失败并回滚。

## 当前差异与未实现范围

- `Contact`、`PreferSmallImage`、联系人照片选择与异步读取/取消尚未提供；没有空实现接口。ProfilePicture、BadgeImageSource 当前使用资源/文件 URI 字符串，交由现有图片管线处理。
- XAML 的 Ellipse 改为圆角 Border，图片使用圆形裁剪和 UniformToFill。Windows 默认人像、群组和 BadgeGlyph 已使用系统 Segoe MDL2 Assets；数字徽章继续使用正文 UI 字体。FontWeight、完整字体测量和原模板部件契约仍待补齐。
- 保留四个头像和三个徽章状态。默认模板已有实际显示和主题切换；任意第三方模板替换、完整样式覆盖和 TemplateSettings 对象 API 尚未完成。
- 缺少系统颜色服务的主题项采用明确的固定调色板；HighContrast 为黑白调色板，不代表完整跟随 Windows 自定义高对比色。
- 显式非方形布局只保证显示区域为圆形，不像上游 SizeChanged 那样回写调用方 Width/Height。自动化名称目前为 en-US，未接入全部语言复数规则；Windows UIA provider 本身仍待实现。

因此该控件是可运行的内部实现切片，不标记为 WinUI 2.3 PersonPicture 全量兼容完成。后续从真实使用和对照测试继续补齐这些差异。

## NumberBox 内联编辑切片

`src/velora/fluent/NumberBox.{hpp,cpp}` 和 `NumberBoxParser.{hpp,cpp}` 直接内化上游 `NumberBox.cpp/.idl`、`NumberBoxParser.cpp/.h` 的数值行为；实际模板为 `resources/Controls/NumberBoxView.xaml`。原始哈希仍见 `porting/Sources.json`，派生文件保留 MIT。原输入不参与构建，正式工厂创建一个实际 NumberBox 对象。

- 支持 Value、Minimum、Maximum、SmallChange、LargeChange、Text、Header、PlaceholderText、IsWrapEnabled、AcceptsExpression、ValidationMode 和 Hidden/Inline 的 SpinButtonPlacementMode。数值默认值沿用此提交的 IDL（Value 为 0、范围为正负最大有限 Double、增量 1/10）；空文本变为 NaN，空值不响应增减。
- Value/Text、范围调整、禁用验证、循环增减和最终 ValueChanged 回调经过现有依赖属性存储。直接 C++、Framework、样式和 TwoWay 使用同一校验/归一化路径；目前范围修正仍为破坏性归一化，不宣称完整 coercion 语义。`addValueChanged` 返回现有属性订阅 token，可用 `removePropertyChanged` 解除；XAML 事件声明仍待扩展。
- 编辑器保留未提交草稿；Enter/失焦验证，Escape 恢复数值显示，Up/Down 和 PageUp/PageDown 使用大小增量。焦点进入选中文本，提交后光标位于末尾。当前只有 KeyDown，因此 Enter/Escape 在按下时生效，上游使用 KeyUp；游戏手柄、完整触摸和滚轮未接入。
- 表达式保留上游的操作符优先级、括号、负数字以及包括 `^` 在内的左结合规则。替换 WinRT 数字解析器后使用有界、无递归的 invariant decimal 解析，支持科学计数法；限制输入长度为 65536 字节，拒绝除零、非有限结果及错误括号，而非将运算错误当作空值。`-(...)` 不支持，与上游负数字分词边界一致。
- 格式化当前为最多 12 位有效数字的 invariant 表示，未实现 DecimalFormatter/显示舍入、本地数字与分组符。取消草稿不会将舍入后的显示值重新写回 Value。
- 模板复用现有 Fluent TextBox/Button 主题，有真实标题、编辑器和内联增减按钮；尚未复刻完整原模板/状态/字体和自定义样式替换。内联按钮暂使用减号/加号，没有 RepeatButton 长按重复。Compact 弹出模式明确拒绝，不能静默退回 Inline。
- 内部 automation 名称来自 Header、类型为 Edit；完整 RangeValue/Value pattern 和 Windows UIA provider 未完成。

Windows 示例 `FluentPage.xaml` 从编译 XAML 创建实际 NumberBox；注册接口为 `NumberBox::registerType`。挂载使用当前 Fluent 编译资源包，失败回滚；卸载解除模板事件订阅并清除借用宿主，重新挂载创建新部件。该切片尚不计为完整 WinUI 2.3 NumberBox 兼容。

## Flyout 按钮族批次

本批一起接通 DropDownButton、SplitButton、ToggleSplitButton，以及它们共享的 Flyout/Popup 路径。按钮逻辑直接内化 `porting/DropDownButton/DropDownButton.cpp`、`porting/SplitButton/SplitButton.cpp`、`ToggleSplitButton.cpp`，模板与资源来自对应 XAML 和 CommonStyles 的 FlyoutPresenter 资源；原始哈希见 `porting/Sources.json`，派生文件保留 MIT。系统提供的 Flyout 底座在 Velora 自行实现，不构建原仓或 WinRT 投影。

- 三个源类型都由编译 XAML 创建实际对象，直接 C++ 工厂也可用。共享 `FlyoutButton` 行为基类；ToggleSplitButton 在 C++ 上继承 SplitButton。类型目录目前只支持内建基类，因此三个声明仍分别继承 UserControl，尚未表达源码类型间的反射继承关系。
- Content 和 IsChecked 使用实际依赖属性；ToggleSplitButton 主动作先切换再发送 Click，次动作只打开 Flyout。Checked 通知可以通过属性 token 解除，初次挂载前设置不会发送激活事件。Click 使用现有 Framework routed event，可通过 `subscribeEvent` 订阅；XAML 的自定义事件声明和 ICommand/CommandParameter 尚未接入。
- Enter/Space 触发主动作，F4/Alt+Down 打开 Flyout；实际次按钮可以单独激活。鼠标激活后焦点归复合控件，关闭后可继续键盘操作。当前按 KeyDown 执行；完整 KeyUp、手柄、设备类型/触摸、取消捕获及所有视觉状态差异仍待补齐。
- `Framework::openPopup` 接管一个独立顶层内容根，沿用同一树与合成器，以内容期望大小放在锚点下方，靠边时翻到上方并限制在窗口内；内容裁剪在 Flyout 圆角 presenter。现阶段每个窗口只允许一个弹出根，替换时先关闭旧根。没有额外 HWND，不支持窗口外弹出、嵌套菜单或完整 Placement API。
- 外部主指针按下关闭且消费整次手势；Escape 关闭；Tab/Shift+Tab 在内容内循环。窗口失焦/最小化、锚点卸载、移除、禁用或隐藏会清理弹出根。关闭先清理服务状态，再移除内容、恢复有效焦点并通知；内容重建、回调中关闭/重开和宿主销毁均按此生命周期处理。
- `Flyout` 是 UI 线程上的共享 C++ 服务对象，用 `std::make_shared<Flyout>(factory)` 创建，再交给按钮的 `setFlyout`。工厂返回新建且无逻辑父节点的内容根，返回后转移树内生命周期，每次打开重新创建。工厂抛出前须自行回滚部分工作，编译组件加载器已有该事务保证；服务会清理自己的 presenter 并通过 `lastError()` 保留诊断。共享 Flyout 可以在控件/窗口之间移动，身份包含 Framework，不能只比较 ElementId。
- Flyout 对象、内容对象属性、关联资源作用域/DataContext 的完整继承，以及 Popup/Flyout 对象的 XAML 语法仍待接入。当前示例在 C++ 中提供内容工厂；不注册假的 XAML Popup 类型。Presenter 与按钮模板仍走正式编译资源包。
- 有真实开闭/勾选模板状态和 Light/Dark/HighContrast presenter 资源；系统调色板、完整默认模板/字体/动画、自定义模板替换、全部状态叠加，以及菜单式内容的完整主题继承尚未复刻。内部 automation 提供 Button 名称/类型，完整 ExpandCollapse/Toggle pattern 与 Windows UIA provider 仍待实现。

本批交付是共用底座与三个可运行控件，不计为 WinUI 2.3 按钮族全量兼容。Gallery 同页展示三个实际源类型、共享 Flyout 内容、主动作和勾选反馈，后续按控件族继续推进。

## MenuFlyout 菜单族接入（待统一验证）

菜单族使用同一个 Flyout 服务，包含 MenuFlyoutItem、ToggleMenuFlyoutItem、MenuFlyoutSeparator。原仓中这部分只有系统控件的主题/模板，C++ 行为在 Velora 内实现；不能声称移植了仓库中不存在的系统控件 C++。资源派生自 `porting/MenuFlyout/MenuFlyout_themeresources.xaml`，MIT 与原始哈希保留。

- MenuFlyout 的集合直接保存实际 UIElement 菜单项，打开时插入同一树，关闭时解除挂载；勾选值保留在各实际对象的依赖属性里，没有菜单模型向另一套控件对象投影。
- 普通项/勾选项有 Text，勾选项另有 IsChecked；C++ `setCommand` 提供动作回调，Click 仍走 Framework routed event。调用时先切换勾选再通知；完成或异常时关闭原弹出根，回调中新开的弹出根不被旧调用关闭。
- 实现 Up/Down、Home/End、Enter/Space 的菜单路径，并复用 Popup 的 Tab/Escape。分隔线不可聚焦，禁用项沿用现有输入过滤。当前仅编译/启动过，未完成这一批的交互回归。
- 集合仅允许关闭且非构建中修改，拒绝重复/已挂载/非菜单项对象；构建失败回滚内容根。菜单项订阅与宿主借用在卸载时解除，菜单持有实际对象，项目状态可在重开时保留。
- Gallery 已改用真实菜单项，含普通动作、勾选项和分隔线；`--open-menu` 在启动时直接加载整组菜单模板，方便启动检查。

子菜单、访问键、完整 ICommand、可观察集合、菜单对象/Items 的 XAML 语法、完整主题与 UIA 尚待实现。单选项、图标和快捷键文字显示已在后续批次接入，见下节；MenuFlyout 继续通过 C++ 服务接入。新增行为仍须在最后统一验证，当前不能计作完整兼容。

## Windows 图标与单选菜单批次（待统一验证）

- 按用户决定，Windows 统一使用系统安装的 **Segoe MDL2 Assets**，不复制字体文件，不处理跨平台字体替代。TextBlock/Button 增加 FontFamily 绘制支持，字体族随 TextCommand 进入 DirectWrite，布局缓存区分字体族；MDL2 图标按 em 单元测量，普通文字的完整跨字体测量仍待补齐。
- 人像/群组使用上游 E77B/E716，箭头使用 E70D，勾选/单选标记使用 E73E/E915。菜单将勾选、图标、正文、快捷键提示拆成模板列，正文保持 Segoe UI。IconGlyph 和 KeyboardAcceleratorTextOverride 使用实际依赖属性；快捷键文字仅显示，不代表已注册快捷键。
- RadioMenuFlyoutItem 直接内化 `porting/RadioMenuFlyoutItem/RadioMenuFlyoutItem.cpp/.idl` 的同父节点、同 GroupName 互斥和点击不反选行为。只保留一个实际 IsChecked 值，没有上游 InternalIsChecked 桥。树外属性设置保留到挂载，打开菜单时对实际兄弟落实分组。
- 新增实际 FontIcon、SymbolIcon，共用 IconElement 和现有绘制管线。FontIcon 提供 UTF-8 Glyph；SymbolIcon 首批支持 Windows SDK 的 14 个常用 Symbol 值并映射到 MDL2，不宣称完整枚举。C++ 使用 `Symbol` 枚举，当前编译 XAML 使用整数值；源码类型具名枚举语法待接入。字体大小、前景和字体族沿用同一对象的现有属性。
- Gallery 包含实际图标控件、菜单图标、快捷键显示及一组单选菜单项；只做示例编译和 `--open-menu` 启动检查，行为、视觉、DPI 和完整回归最后统一执行。旧资源包需随 schema 变化正常重编，不关闭版本或未知语法诊断。

## MenuBar 与 AppBar 按钮批次（待统一验证）

新增 MenuBar、MenuBarItem、AppBarButton、AppBarToggleButton 四种实际源码类型、编译模板与 Gallery 入口。MenuBar/MenuBarItem 直接参考 `porting/MenuBar/` 的固定 2.3 C++/XAML；AppBar 按钮模板派生自 `CommonStyles/AppBarButton_themeresources.xaml` 和 `AppBarToggleButton_themeresources.xaml`，系统控件行为在 Velora 实现。来源哈希仍见 `porting/Sources.json`，派生代码/模板保留 MIT。

- MenuBar 集合持有实际 MenuBarItem，支持树外挂载及挂载后 append/clear，拒绝重复和已挂载对象。模板使用现有横向布局；Header、Label、IconGlyph、IsCompact、IsChecked 读取对应实际依赖属性，内容别名不建立第二份值。
- MenuBarItem 复用 FlyoutButton 和 MenuFlyout，菜单不为空时才展开。实现点击开闭、上下键展开、左右键循环/Home/End 导航、跳过隐藏/禁用标题，以及菜单已展开时的标题悬停切换。展开状态从实际 Flyout 计算，父关联是弱引用；卸载和失败挂载沿用同一树的清理。
- Popup 增加受限的祖先 pass-through 区域：菜单内容优先命中，其余只允许菜单栏区域接收输入；左右键可路由到该菜单栏。其余外部区域仍关闭并消费手势，不把菜单透传扩展成全窗口点击穿透。菜单切换继续使用同一窗口的单个弹出根，不实现第二套弹出树或嵌套菜单。
- AppBar 按钮使用系统 MDL2 图标和下方标签；IsCompact 隐藏标签并缩小最小高度，Toggle 类型在 Click 前更新 IsChecked，普通类型可关联 Flyout。Click 和可解除的勾选通知复用现有事件/属性系统。
- 示例 `--open-menubar` 可在启动时创建菜单栏及其内容；普通 `--open-menu` 保留。菜单栏和 AppBar 按钮本身可以由编译 XAML 创建，Items、Menu/Flyout 对象目前通过 C++ 接入。

本批仅编译并启动检查。完整键盘/悬停交互、模板状态叠加、主题/DPI/辅助功能及回归尚未统一验收；F10/Alt 访问键、RTL、滚动/变换中的透传边界、可观察集合、Items 的 XAML 语法、AppBar 完整 LabelPosition/IconSource/ICommand 等仍待实现。CommandBar 的后续接入见下一节，不能据此宣布完整 WinUI 2.3 兼容。

## CommandBar 与显式溢出批次（待统一验证）

新增 CommandBar、AppBarSeparator 及主命令/溢出命令编译模板。参考固定 2.3 的 `CommonStyles/CommandBar_themeresources.xaml`、AppBar 按钮与分隔线主题资源；CommandBar 属于系统 XAML 控件，上游主题资源不包含其完整系统 C++ 实现。本批在 Velora 实现对应的基础行为，不宣称已经直接迁移全部系统实现。派生文件保留 MIT 和来源说明。

- PrimaryCommands、SecondaryCommands 持有实际 AppBarButton/AppBarToggleButton/AppBarSeparator 对象，通过 C++ append/clear 设置；拒绝重复对象、已挂载对象、跨线程调用及溢出展开时 append。清空集合会先关闭溢出面板。
- 次命令展开时挂载原对象，关闭时卸载，勾选值继续保留在原依赖属性；IsInOverflow 为实际只读依赖属性，不创建菜单代理或第二份按钮状态。AppBar 按钮在主区域显示图标和下方标签，在溢出区域显示横排图标与标签。分隔线方向由模板优先级设置，本地 Orientation 保持更高优先级。
- CommandBar.IsOpen 经既有 Flyout/Popup 同步开闭、点击关闭和卸载；More 按钮使用 Segoe MDL2 Assets。实现主区域左右/Home/End、More 的上下/F4 展开、溢出区域上下/Home/End 路径，Escape/Tab 沿用 Popup。AppBar 按钮新增 C++ setCommand 回调，沿用 Toggle 更新、Click 和既有属性/绑定路径。
- Gallery 用 `--open-commandbar` 打开包含普通命令、勾选命令及分隔线的实际溢出面板。本批仅编译和一次原生启动/关闭检查，没有新增或执行回归、ASan 或截图验收。

本批最初仅支持显式次命令，后续宽度驱动的接入见下一节。DynamicOverflowOrder/通知、CommandBarFlyout、Content/ClosedDisplayMode、可观察集合与集合 XAML、完整 ICommand/图标属性、嵌套弹出与完整主题/视觉状态/UIA 仍待实现。键盘、焦点、命令执行顺序和销毁期间的完整行为将在集成末尾统一验证，不能将构建/启动成功算作完整兼容。

## CommandBar 动态溢出（待统一验证）

- 新增默认开启的 IsDynamicOverflowEnabled 实际依赖属性及 C++/编译 XAML API。测量主命令自然宽度，必要时预留 More 按钮空间，从末尾收纳主命令；恢复时沿用 PrimaryCommands 的原始顺序。收纳段先于显式 SecondaryCommands 显示。
- 当前 `.vtypes` 的源码属性契约仍限定 Paint，未修改该格式或放宽契约检查。新增线程检查的 ElementTree::invalidateMeasure，CommandBar 在相关有效值变化时显式请求重新测量，确保配置改变及弹出关闭后重新分配命令。
- 关闭状态下，收纳命令保留在同一树内的折叠存储容器，可参与自然尺寸测量；元素树新增 reparent，保留对象、元素 ID 和子树，更新继承父级及布局失效。现有 adopt 继续仅接受独立根。打开面板时，仍用原对象加载溢出模板，关闭后在下一次布局恢复主区域模板；没有创建菜单代理或影子勾选值。
- IsInOverflow 反映按钮的收纳位置；当前加载的模板单独记录为呈现状态，避免折叠存储中的主命令误按溢出模板尺寸测量。聚焦命令被收纳时尝试将焦点转交 More；面板展开期间宽度变化或关闭动态溢出会先关闭面板，再重新分配。
- Gallery 增加一组实际 MDL2 命令；`--open-commandbar` 在启动时确认末尾主命令进入溢出，再打开含主/次命令的面板。

此批最初按原始顺序收纳后缀；后续优先级分组与通知见下一节。分隔线合并、RTL、展开期间原位重排、完整焦点/卸载/绑定继承行为仍待统一验证。不能据此宣布 CommandBar 或元素树重挂载接口已完成全部兼容验收。

## 溢出优先级分组与通知（待统一验证）

参考系统 [AppBarButton.DynamicOverflowOrder](https://raw.githubusercontent.com/MicrosoftDocs/winrt-api/docs/windows.ui.xaml.controls/appbarbutton_dynamicoverfloworder.md)、[AppBarSeparator.DynamicOverflowOrder](https://raw.githubusercontent.com/MicrosoftDocs/winrt-api/docs/windows.ui.xaml.controls/appbarseparator_dynamicoverfloworder.md) 与 [CommandBar 的变化事件](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.controls.commandbar.dynamicoverflowitemschanging)。这是系统契约的 Velora 实现，来源不变；不宣称 WinUI 2.3 仓库包含该系统控件的全部 C++ 算法。

- AppBarButton、AppBarToggleButton、AppBarSeparator 均具有 DynamicOverflowOrder 实际依赖属性及 C++/编译 XAML API。默认 0；Velora 当前校验为非负 32 位整数。正数从小到大整组收纳，同一正数一起移入/恢复；0 保留末尾逐项收纳。主区及溢出区内部均保留原始命令相对顺序，PrimaryCommands/SecondaryCommands 不改写。
- 用逐项派生布局位置替代前缀计数，允许前部或中部命令先溢出；键盘遍历与面板构建共用该结果。实际按钮仍只有原依赖属性中的 IsInOverflow/IsChecked，布局位置记录不成为第二份可写属性。
- C++ addDynamicOverflowItemsChanging 返回独立 token，由 removeDynamicOverflowItemsChanging 解除；通知发生在布局移动前，参数包含加入/退出方向与实际对象集合。分发中解除的处理器不再调用，新处理器从下次分发开始；卸载中止剩余通知。回调中拒绝修改命令集合或打开面板；此入口用于观察计划，布局属性的回调内反复写入、取消或强制重布局尚未实现完整契约。
- 监听主命令的有效优先级变化，以弱引用关联 CommandBar，关闭已展开面板并请求布局；清空或卸载解除监听。追加失败回滚新增监听和元素。事件目前仅 C++ 连接，不增加未知 XAML 的容错或伪造事件元数据。
- Gallery 将 Save、分隔线、Pin 设为正数同组，末尾普通命令保持默认优先级；启动入口检查非后缀分组并打开混合溢出面板。本批只编译与简单启动/关闭，恢复、动态优先级更新、通知解绑/重入和完整生命周期留到统一验证。
