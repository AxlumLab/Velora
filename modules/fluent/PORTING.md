# Fluent 内部控件

WinUI 2.3 的逻辑、模板和资源直接改写为本模块的普通 C++ 控件及编译 XAML。`controls` 提供树、属性、布局、输入、模板和渲染底座；本模块拥有控件行为。不会构建微软上游运行时或保留 WinRT 投影。

来源固定为 microsoft/microsoft-ui-xaml 的 `v2.3.191129002`、提交 `2237b09d9ba6052414ddccf3d61316de20564303`。原文件哈希见 [WinUI23.provenance.json](WinUI23.provenance.json)，完整 MIT 文本见 [LICENSE-WinUI2.3.txt](LICENSE-WinUI2.3.txt)。许可证同时安装到 SDK 并包含在 Fluent 资源包中。

完整上游镜像已从受版本管理的源码树移除。本机参考缓存位于忽略的 `build/reference/winui2.3/`，不是依赖或交付物；干净工作区可按来源链接取得具体参考文件，不必重新把整仓加入项目。

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
