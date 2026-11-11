# Velora 本地 Agent 指令

这是重新整理后的源码基线。先读 [START-HERE.md](START-HERE.md)、[docs/STATUS.md](docs/STATUS.md)，再读当前任务涉及的设计。不要恢复旧阶段计划、COM 式包装或占位控件。

## 确定的产品方向

- C++23 + XAML，面向新应用。保留 WPF/UWP 的属性、绑定、模板、资源与代码后台体验，不要求旧项目源码或 WinRT 二进制兼容。
- 第一批产品平台为 Windows、Android。Windows 使用 Win32 + D2D + DirectWrite；Android 计划使用锁定版本的 Impeller standalone。其他平台暂不实现。
- 视觉与交互目标为 WinUI 2.3 历史 Gallery。现有基础 Gallery 不代表完整验收。
- Windows 图标统一使用系统安装的 Segoe MDL2 Assets；暂不设计跨平台字体替代方案。
- WinUI 2.3 的控件逻辑、模板与资源直接内化到 modules/fluent/，成为 Velora 内部实现；controls 提供通用 UI 底座。不得把整个上游仓库镜像纳入产品源码并当作控件移植。派生文件保留 MIT 版权及来源，见 modules/fluent/PORTING.md。
- 性能平衡启动、响应、内存、包体和功耗。Windows 用等价 UWP 负载对照，先测量再设目标。
- 源码控件随应用构建、按需裁剪；计划支持无需重编宿主的原生插件，限定同一 SDK 构建及工具链配置。
- 编辑器第一版做 XAML 文本编辑、预览和错误定位，允许重建页面。C++、生成连接契约改变可要求原生重编。

## 对象与扩展

- 普通 C++ 类、继承、virtual 扩展点和 RAII，不引入 Qt 依赖。
- 已放弃跨任意工具链/版本的稳定对象 ABI。不得恢复 QueryInterface、IID、两指针引用、逐方法函数表、逐控件投影、激活桥或自定义引用计数协议。
- TypeId、属性 ID、反射元数据、XAML 二进制版本仍然需要，不要误删。
- D2D、DirectWrite、TSF 中真实 Windows API 使用的 COM 必须保留。
- 每个控件只有一个实际对象、一份属性有效值和一套行为来源。Framework/ElementTree 仍是现状，不新建平行运行时或影子属性。
- 过渡期沿用树中的 shared_ptr 所有权。父回指、事件订阅和异步回调避免强引用环。不要同时采用父对象 delete 子对象和 shared_ptr 管理同一对象。
- UI 对象具有线程亲和性。生命周期必须包含卸载、解除订阅、异步取消和 UI 线程销毁，见 PLUGIN-SDK.md。shared_ptr 不等于线程安全。
- 动态插件使用应用携带的共享运行时与唯一注册服务，禁止每个插件静态链接第二份运行时。第一版不热卸载原生插件。

## 实施原则

- 区分当前实现、设计待实现和实际验证。空返回、空处理器、类名存在或构建成功均不等于功能完成。
- 从 [docs/IMPLEMENTATION-PLAN.md](docs/IMPLEMENTATION-PLAN.md) 选择一个可验收步骤，先读实际代码再改，完成这一小步再推进。
- 保留依赖属性、绑定、模板、资源、布局、输入、渲染及有效测试。删除包装时把行为覆盖迁到实际接口，不删测试掩盖回归。
- 部分控件属性仍由 Framework::set 路由。修改范围值、Toggle、单选组等，不得绕过校验、事件和绑定写回。
- 正常应用使用编译后的 XAML；ANTLR 属于构建期。未知语法、类型和属性必须有诊断。
- 模块边界见 DESIGN.md。第一阶段仅封装 UI 必需的平台服务，不实现完整 WinRT 或通用 OS API。
- 第三方依赖通过 tpkg。保留锁定版本和现有版权/来源声明，不以绕过依赖或关闭测试解决构建问题。
- 生成代码放 build/<preset>/generated，修改生成器和输入，不手改生成输出。golden 可随明确的格式变更更新。
- 按风险验证改动。无平台环境时准确说明限制，不把 Linux 核心构建写成 Android 或 Windows 上屏验收。

## 代码与构建

- C++23，Allman 花括号、4 空格、UTF-8/LF。类型 PascalCase，方法 lowerCamelCase，成员 m_，静态成员 s_，常量/枚举项 k 前缀。遵循周边代码，不批量改名。
- 使用 globals.hpp 的固定宽度别名。不假设 long、指针、wchar_t 跨平台等宽，UTF-16 使用 char16_t。
- 保持现有扁平 velora 命名空间。显式包含所用标准头，头文件不注入全局 using namespace。
- 头文件放声明、必要模板、小型内联定义，非平凡实现放 .cpp。按职责拆分，不按行数拆分。
- 源码在 modules/<module>/src/velora/，通过 velora_module 注册；不另建顶层 src。
- 用 CMakePresets.json 构建，命令见 [docs/BUILD.md](docs/BUILD.md)。按当前用户要求，接入阶段只编译并做简单启动检查，完整回归/ASan/截图验收统一留到最后；保留已有测试。源码规则用 CheckStyle.py、CheckWorkspace.py 检查。
- 只格式化改动的手写代码。临时工具、日志、缓存不进源码包。

## 完成报告

说明变更、理由、实际验证和剩余限制，同步 STATUS.md。不要另造阶段报告森林，也不要把未来计划标成完成。

按用户要求，每次结束都汇报尚未接入、部分接入和完整兼容验收的剩余数量，注明使用功能组口径，不将接入当作完整迁移。
