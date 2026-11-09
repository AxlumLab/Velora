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

下一批先从 Toggle/Range 基础类、主题/模板与弹出层补齐开始，再接入 NumberBox/SplitButton/DropDownButton，随后推进集合/虚拟化及导航控件。每批以可运行控件和相关回归为交付，已导入输入不计为可用控件。

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
- XAML 的 Ellipse 改为圆角 Border，图片使用圆形裁剪和 UniformToFill。MDL2 默认人像/群组符号暂改为简单几何图形；BadgeGlyph 使用当前文本字体。FontFamily、FontWeight 与完整原模板部件契约仍待补齐。
- 保留四个头像和三个徽章状态。默认模板已有实际显示和主题切换；任意第三方模板替换、完整样式覆盖和 TemplateSettings 对象 API 尚未完成。
- 缺少系统颜色服务的主题项采用明确的固定调色板；HighContrast 为黑白调色板，不代表完整跟随 Windows 自定义高对比色。
- 显式非方形布局只保证显示区域为圆形，不像上游 SizeChanged 那样回写调用方 Width/Height。自动化名称目前为 en-US，未接入全部语言复数规则；Windows UIA provider 本身仍待实现。

因此该控件是可运行的内部实现切片，不标记为 WinUI 2.3 PersonPicture 全量兼容完成。后续从真实使用和对照测试继续补齐这些差异。
