# 本次清理记录

## 目的

生成可直接导入空工作区的完整源码包，同时移除为了稳定对象 ABI 增加的 COM 式包装。工程采用普通 C++ 类和受控 SDK 配置的扩展方向，设计与实现状态分别记录。

## 删除

- 自建对象 ABI：InterfaceRef、接口查询/函数表、手工引用/弱引用控制、对象分配域、逐控件投影及桥接能力层。
- 独立 RuntimeContext/RuntimeObject/ButtonObject 实验路径，避免与真实 Framework/ElementTree 重复。
- ABI 专用生成器、注解处理、元数据、CMake 生成链、DLL 测试提供者、冻结清单和只检验旧协议的测试。
- 未接入实际 UI 树的复合控件草稿及对应空 Gallery 页面。保留真实 ListView/Items 模板支持，不以名称批量删除代码。
- 旧 docs/tasks、阶段计划/完成报告、嵌套补丁压缩包、旧补丁覆盖脚本和相互冲突的 AGENTS 规则。

这里删除的高级控件草稿需要按新设计重新实现，不计为可用功能损失后的替代交付。

## 保留与迁移

- 实际 UI 树、依赖属性、绑定、样式/模板、布局、输入、渲染、XAML 编译加载和资源系统保留。
- TypeId 从旧 object 目录移到 foundation 的 types/，继续用于普通类型身份；RuntimeTypeRegistry 使用简单 C++ Entry。
- 原先混合测试里的控件行为断言迁到 Framework/UIElement/ElementTree；只有纯旧协议断言被移除。
- 真正的 Windows COM 调用仍在 D2D、DirectWrite、TSF 后端中。
- 原版权/来源 NOTICE 保留。新测试输入明确标注为本次新建，不冒称恢复原 SDK 原件。

## 顺带修复的实际问题

- Foundation UTF 编码工具原本写死 Windows。本次改为严格 UTF-8/UTF-16 转换，UTF-16 类型为 std::u16string/std::u16string_view。DirectWrite 的宽字符 helper 改名 utf8ToWide，避免重复同名实现。
- 移除桥接后，把非有限范围值、无效 Slider 步长等校验留在实际 setter；ScrollViewer::changeView 对非法数值原子拒绝。
- CMake XAML helper 的 velorac 路径自引用改为实际工具目标。
- 资源包集成测试明确选择 EXTERNAL，测试路径与部署的 Resources/App.vpak 一致。
- 补齐原包缺失的 BasicTypes.hpp、Nested.hpp、Player.hpp 反射输入；测试明确拒绝缺文件、空文件和零类型，防止空目录假通过。声明预期保留，golden 源行号随新输入更新；完整 ANTLR 验证待本机执行。
- 基础 Gallery 的页面根挂载/移除、列表、范围绑定和绘制样例整理为实际路径，删除无作用的交互入口。

## 新的文档入口

根目录只保留 README、START-HERE、AGENTS 三个主要入口。docs/ 保存总体设计、插件 SDK、XAML 工具链、实施计划、构建、现状及本记录。新增格式与忽略配置、源码布局检查之外的旧包装检查脚本。

本包不携带构建目录、依赖缓存、下载工具、临时验证程序或旧生成代码。具体实测结果和限制见 STATUS.md。

## 使用影响

旧 InterfaceRef/投影/桥接 API 已有意删除，不能继续链接旧对象文件或二进制插件。请在空目录重新配置编译。普通类型身份保留不表示旧二进制协议仍可用。

当前仍是静态库构建。Qt 式普通 C++ 插件方向已经写成设计，并未通过添加空类或假加载器宣称实现。新 SDK 和 Android 由本地 agent 按实施计划逐步完成。
