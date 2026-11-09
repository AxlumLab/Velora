# 当前状态与验证

基线日期：2026-11-09。来源为用户提供的 velora.zip，经本次清理后形成独立源码树。本文记录实际状态；DESIGN.md 和其他设计文档中的“待实现”不能视为交付。

## 已保留的实现

| 范围 | 当前事实 |
| --- | --- |
| 控件对象 | UIElement/BuiltinElements、createElement、shared_ptr 元素树和 virtual 布局已有实现；一部分行为仍集中在 Framework |
| 属性与交互 | DependencyObject、属性优先级、Binding、TwoWay 写回、事件、焦点、范围值、滚动与内容生命周期代码保留 |
| XAML | 二进制 IR、读写、加载器、代码后台生成、资源/模板/状态相关代码保留；编译 schema 仍以内建控件为主 |
| 资源 | URI、限定条件、.vpri 索引、.vpak、压缩与 loose/packed 访问保留；包可嵌入应用 |
| Windows | Win32、D2D、DirectWrite、TSF 和应用窗口接入源码保留，本次没有 Windows 环境实测 |
| Gallery | 单页基础控件示例已整理，删除无作用导航和空页面；不是完整 WinUI 2.3 Gallery |
| 普通类型注册 | RuntimeTypeRegistry 记录 TypeId、类名、基类，校验重复/未知基类/继承环；没有插件工厂或动态加载器 |
| 元数据 | .rkmeta 写入 schema 5，普通 v3/v4 元数据仍可读；旧 abi-* 行不再接受 |

## 未实现或尚未完整验收

2026-11-09 本地接续：已阅读设计并抽查 UIElement、ElementFactory、RuntimeTypeRegistry、Framework 属性路由和 Gallery；用户进一步明确 WinUI 2.3 完整行为覆盖与上游控件逻辑移植要求，执行原则已补入 IMPLEMENTATION-PLAN.md。本机执行 CheckStyle.py、CheckWorkspace.py 均通过（0 issue）。本轮尚未重新执行编译、CTest 或 UI 验收，以下历史验证结论不代表本机验证。

- Android：没有完整平台宿主、Impeller 后端、NDK preset、IME/辅助功能适配。
- 动态控件插件：缺共享库 SDK、导出配置、插件匹配与加载、开放工厂及输入/绘制扩展契约。
- 源码按需裁剪：有模块拆分，不等于已经实现控件与资源的完整依赖裁剪。
- Gallery：复合控件、完整主题模板和历史 2.3 清单尚待补齐。Fluent 包只是部分资源，不能据此宣称完整视觉一致。
- C++ 开发体验：移除了包装，仍需把常用属性与控件行为整理成直观的类接口。
- 编辑器：没有可用文本编辑/编译/预览进程；文档给出重建式预览设计。
- 跨线程生命周期：当前 shared_ptr 与弱令牌不等于完整 UI 线程销毁协议，插件交付前必须落实并验证。

## 本次实际执行的验证

环境：Linux、GCC 14.2.0、C++23、CMake 4.4.4、Ninja，Debug core-check，ANTLR 关闭。

- 完整 core-check 构建通过，含该配置下的公开头文件独立编译。
- CTest **17/17 通过**：基础类型/内存、反射消费与边界、数据、编码、元素树、合成、视觉桥、Framework、ScrollViewer、基础控件、HAL 骨架、资源 URI、资源包、类型注册、依赖属性、控件核心。
- CheckStyle.py、CheckWorkspace.py 通过。后者检查旧自建对象包装的路径/符号，不屏蔽真实 Windows COM。
- 编码测试额外以 Debug 和 `-O2 -DNDEBUG` 实际运行通过，覆盖非 BMP、嵌入 NUL、非法 UTF-8 与孤立代理项。
- 21 个不依赖生成 ANTLR 头的相关编译器/元数据/XAML 编译单元通过 GCC C++23 语法检查。
- 实际 MetadataCatalog 的独立检查通过：普通 v3/v4 兼容、v5 回环、旧 ABI 行拒绝，以及新测试输入对应的五类型元数据、跨类型属性路径和只读限制。
- 新增的三个反射测试输入作为 C++ 头文件通过语法与签名检查；对应两个测试编译单元语法检查通过。
- Gallery XAML 的 XML 结构、x:Name 与测试类型/事件引用进行了静态核对。

## 明确未执行

未准备 tpkg 的 ANTLR runtime 与 tool jar，因此没有运行 ANTLR frontend、完整 velorac 生成、编译器 golden、安装消费测试或资源包 CMake 集成测试。新反射输入的 frontend 输出与 golden 仍需完整配置验证。

没有运行 Windows Debug/Release 全量构建、D2D/DWrite 上屏、Gallery 交互、TSF/IME、Windows ASan 或 Android 真机测试。资源包测试部署路径与 Windows UTF 转换调用经过源码检查，但不能代替这些平台验证。

本地 agent 从 IMPLEMENTATION-PLAN.md 第 0 步接续，先补齐本机能够执行的验证，再实施新架构。
