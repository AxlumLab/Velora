# 控件插件 SDK 设计

状态：设计待实现。当前只有普通类型注册与控件运行时，不存在已交付的动态插件加载器。下述 API 均为设计草图。

## 1. 扩展模型

采用类似 Qt 的普通 C++ 类库与 virtual 扩展点，不引入 Qt 依赖，也不追求跨工具链稳定 ABI。Qt 本身具有明确的二进制兼容政策，并限制工具链、系统环境和构建配置；Velora 初期采用更严格的精确构建匹配。[Qt 插件模型](https://doc.qt.io/qt-6/plugins-howto.html)、[Qt 兼容政策](https://doc.qt.io/qt-6/qt-releases.html)

插件可以提供新的 XAML 控件，包含自定义 Measure/Arrange、命中与输入处理、绘制及模板资源。它继承同一套 UIElement/Control，使用同一依赖属性与事件系统；绘制输出框架 Canvas/DisplayList，不直接把 Windows 或 Impeller 句柄放进通用控件接口。

源码模块与动态插件发布相同声明元数据，分别采用静态注册与动态注册。源码形式支持裁剪；动态形式无需重新链接宿主即可安装兼容的新控件包。新控件能被动态 XAML 加载，不等于已有 C++ 代码可免编译调用其未知类型。

## 2. 包与兼容检查

每个插件包包含清单、声明元数据、目标平台二进制和可选 `.vpak`。声明元数据可供编译器与编辑器读取，不要求加载原生库。

清单至少记录插件标识、版本、SDK build ID、类型模块标识、元数据版本/摘要、二进制路径及资源包。构建匹配键由 SDK CMake 自动生成，包含编译器及版本、目标系统/架构、标准库和 CRT、Debug/Release、RTTI/异常、迭代器检查与影响布局的选项。初期要求 SDK build ID 和匹配键完全一致；禁止仅比较产品版本或架构就放行。

加载顺序：读取清单并验证匹配 → 加载本地二进制 → 复核导出的 build ID → 创建插件 → 暂存类型/属性/工厂/资源注册 → 校验重复声明、基类、属性契约和元数据摘要 → 一次提交。任何阶段失败均不发布半份注册，也不创建页面对象。诊断需指出实际值、期望值及重新构建命令。

## 3. 入口与扩展点

设计草图，未实现，命名可在实现前统一：

```cpp
namespace velora
{
class PluginContext;

class Plugin
{
public:
    virtual ~Plugin() = default;
    virtual bool registerTypes(PluginContext& context) = 0;
};
}

extern "C" const char* veloraPluginBuildId() noexcept;
extern "C" velora::Plugin* veloraCreatePlugin(velora::PluginContext&) noexcept;
extern "C" void veloraDestroyPlugin(velora::Plugin*) noexcept;
```

少量 C 导出仅提供固定符号入口。类型操作直接使用匹配 SDK 的 C++ 类，不引入逐方法函数表或逐控件投影。导出函数内部捕获错误；创建失败返回空值，通过 context 记录诊断。宿主将注册异常转为诊断并回滚。插件对象由创建它的插件销毁，宿主负责在 UI 线程安排销毁时机。

`PluginContext` 由宿主传入，提供唯一注册表、资源管理器、UI 工厂和诊断。工厂返回带正确 UI deleter 的共享对象；插件不得直接向公共调用者泄露拥有关系不明的裸对象。自定义控件的布局、指针/键盘/文本输入与绘制入口是后续工作，现有仅布局 virtual 接口不足以宣称插件完成。

## 4. 驻留与共享状态

初期成功载入的插件库驻留到进程结束，更新插件需要重启。关闭页面可销毁控件，不能卸载仍可能被访问器、缓存、工厂或 deleter 引用的代码。预览器重启进程即可更新插件，不实现原生库热卸载。

动态配置统一链接应用携带的共享运行时。注册表、资源管理器、类型身份、分配/销毁服务必须由这一运行时提供；检测到重复运行时实例时拒绝注册。不同窗口可以有独立 XamlRoot 与资源上下文，但不能有不同的插件类型定义。Android 首版插件随应用包按架构部署。

## 5. 首个可交付验收

实现一个宿主编译时不知道的 Meter 控件插件：声明数值 DP、改变期望尺寸、响应指针和键盘、绘制变化内容，并提供默认模板。验收必须证明：

1. Windows DLL 与 Android `.so` 分别被对应宿主加载，同一 XAML 使用相同类型名。
2. XAML 读取声明并创建控件，Binding/ThemeResource 与原生控件共享语义，窗口关闭后订阅解除。
3. SDK、CRT 或 Debug 配置不匹配时，在调用 C++ 工厂前明确拒绝；重复类型注册事务回滚。
4. 工作线程释放最后引用时，析构确实在 UI 线程发生；插件仍驻留。
5. 源码静态配置可移除该控件及专属资源；动态配置无第二份运行时。

这组验收完成前，文档和 Gallery 只标记“插件设计中”，不展示不可用的安装入口。
