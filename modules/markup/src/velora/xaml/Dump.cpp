#include "velora/xaml/Parser.hpp"

#include <set>
#include <sstream>

namespace velora
{
namespace
{

std::string quoted(std::string_view s)
{
    std::string out = "\"";
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                static constexpr char kHex[] = "0123456789abcdef";
                out += "\\x";
                out += kHex[c >> 4];
                out += kHex[c & 15];
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    return out + "\"";
}

std::string at(const Span& s)
{
    return "@" + std::to_string(s.begin.line) + ":" + std::to_string(s.begin.column) + "-" +
           std::to_string(s.end.line) + ":" + std::to_string(s.end.column);
}

std::string namespaceName(const QName& q)
{
    std::string out = q.ns == kXamlPresentationNs    ? "presentation"
                      : q.ns == kXamlNs              ? "x"
                      : q.ns == kXamlDesignNs        ? "d"
                      : q.ns == kXamlCompatibilityNs ? "mc"
                      : q.ns == kXmlNs               ? "xml"
                                                     : q.ns;
    if (!q.condition.empty())
        out += "?" + q.condition;
    return out;
}

std::string written(const QName& q)
{
    return q.prefix.empty() ? q.local : q.prefix + ":" + q.local;
}

class Dumper
{
public:
    std::string str() const
    {
        return m_out.str();
    }

    void element(const XamlElement& e, int depth)
    {
        line(depth) << (e.propertyElement ? "property-element " : "element ") << written(e.name);
        if (!e.name.ns.empty())
            m_out << " ns=" << namespaceName(e.name);
        if (e.ignored)
            m_out << " ignored";
        if (e.preserveSpace)
            m_out << " xml:space=preserve";
        m_out << ' ' << at(e.span) << '\n';
        for (const auto& a : e.attributes)
            attribute(a, depth + 1);
        for (const auto& child : e.children)
        {
            if (const auto* nested = std::get_if<std::shared_ptr<XamlElement>>(&child))
                element(**nested, depth + 1);
            else if (const auto* t = std::get_if<XamlText>(&child))
                line(depth + 1) << (t->cdata ? "cdata " : "text ") << quoted(t->value) << ' ' << at(t->span)
                                << '\n';
            else if (const auto* c = std::get_if<XamlComment>(&child))
                line(depth + 1) << "comment " << quoted(c->text) << ' ' << at(c->span) << '\n';
        }
    }

    void comment(const XamlComment& c)
    {
        m_out << "comment " << quoted(c.text) << ' ' << at(c.span) << '\n';
    }

private:
    std::ostringstream& line(int depth)
    {
        m_out << std::string(static_cast<std::size_t>(depth) * 2, ' ');
        return m_out;
    }

    void attribute(const XamlAttribute& a, int depth)
    {
        static constexpr const char* kKinds[] = {"property", "attached", "directive",
                                                 "xmlns",    "xml",      "ignored"};
        line(depth) << "attr " << written(a.name) << ' ' << kKinds[static_cast<int>(a.kind)];
        if (!a.name.condition.empty())
            m_out << " if=" << a.name.condition;
        m_out << ' ' << quoted(a.value) << ' ' << at(a.span) << '\n';
        if (a.markup)
            markup(*a.markup, depth + 1);
    }

    void markup(const MarkupExtension& m, int depth)
    {
        line(depth) << "markup " << (m.kind.empty() ? written(m.name) : m.kind) << ' ' << at(m.span) << '\n';
        if (m.positional)
            argument(*m.positional, depth + 1);
        for (const auto& a : m.named)
            argument(a, depth + 1);
        if (m.bind)
            bind(*m.bind, depth + 1);
    }

    void argument(const MarkupArgument& a, int depth)
    {
        line(depth) << (a.name.empty() ? std::string("positional") : "arg " + a.name);
        if (a.value.extension)
        {
            m_out << ' ' << at(a.value.span) << '\n';
            markup(*a.value.extension, depth + 1);
        }
        else
        {
            m_out << ' ' << quoted(a.value.text) << ' ' << at(a.value.span) << '\n';
        }
    }

    void path(const BindPath& p, int depth)
    {
        line(depth) << "path " << at(p.span) << '\n';
        for (const auto& s : p.steps)
            line(depth + 1) << (s.kind == BindStep::Kind::kMember ? "member " : "index ") << quoted(s.name)
                            << ' ' << at(s.span) << '\n';
    }

    void bind(const BindExpression& b, int depth)
    {
        line(depth) << (b.call ? "bind-call " : "bind ") << at(b.span) << '\n';
        path(b.path, depth + 1);
        static constexpr const char* kLiterals[] = {"int", "float", "string", "bool", "null"};
        for (const auto& argument : b.arguments)
        {
            if (const auto* p = std::get_if<BindPath>(&argument))
                path(*p, depth + 1);
            else
            {
                const auto& l = std::get<BindLiteral>(argument);
                line(depth + 1) << "literal " << kLiterals[static_cast<int>(l.kind)] << ' ' << quoted(l.text)
                                << ' ' << at(l.span) << '\n';
            }
        }
    }

    std::ostringstream m_out;
};

} // namespace

std::string dumpXaml(const XamlDocument& document)
{
    Dumper d;
    for (const auto& c : document.comments)
        d.comment(c);
    if (document.root)
        d.element(*document.root, 0);
    return d.str();
}

// The WinUI presentation types the Velora XAML subset accepts as object elements. Types in
// using: namespaces are application types, checked against reflection metadata later.
bool isBuiltinType(const QName& name)
{
    static const std::set<std::string_view> presentation = {
        // Pages and layout
        "Page",
        "UserControl",
        "Grid",
        "StackPanel",
        "Canvas",
        "Border",
        "RelativePanel",
        "Viewbox",
        "ScrollViewer",
        "ScrollContentPresenter",
        "SplitView",
        "RowDefinition",
        "ColumnDefinition",
        // Content and items
        "ContentControl",
        "ContentPresenter",
        "ItemsControl",
        "ItemsPresenter",
        "ListView",
        "ListViewItem",
        "GridView",
        "GridViewItem",
        "ListBox",
        "ListBoxItem",
        "FlipView",
        "Pivot",
        "PivotItem",
        "NavigationView",
        "NavigationViewItem",
        // Text
        "TextBlock",
        "RichTextBlock",
        "Paragraph",
        "Run",
        "Span",
        "Bold",
        "Italic",
        "Underline",
        "LineBreak",
        "Hyperlink",
        // Controls
        "Button",
        "RepeatButton",
        "ToggleButton",
        "HyperlinkButton",
        "CheckBox",
        "RadioButton",
        "ToggleSwitch",
        "Slider",
        "ProgressBar",
        "ProgressRing",
        "TextBox",
        "PasswordBox",
        "ComboBox",
        "ComboBoxItem",
        "DatePicker",
        "CalendarDatePicker",
        "TimePicker",
        "ColorPicker",
        "Thumb",
        "ScrollBar",
        "ToolTip",
        "Flyout",
        "MenuFlyout",
        "MenuFlyoutItem",
        "MenuFlyoutSeparator",
        "Popup",
        "Image",
        "FontIcon",
        "SymbolIcon",
        "PathIcon",
        "BitmapIcon",
        // Shapes
        "Rectangle",
        "Ellipse",
        "Line",
        "Path",
        "Polygon",
        "Polyline",
        // Resources, styles, templates
        "ResourceDictionary",
        "StaticResource",
        "Style",
        "Setter",
        "ControlTemplate",
        "DataTemplate",
        "ItemsPanelTemplate",
        "SolidColorBrush",
        "LinearGradientBrush",
        "GradientStop",
        "AcrylicBrush",
        "Thickness",
        "CornerRadius",
        "FontFamily",
        "Color",
        // Visual states and animation
        "VisualStateManager",
        "VisualStateGroup",
        "VisualState",
        "VisualTransition",
        "StateTrigger",
        "AdaptiveTrigger",
        "Storyboard",
        "DoubleAnimation",
        "ColorAnimation",
        "PointAnimation",
        "DoubleAnimationUsingKeyFrames",
        "ColorAnimationUsingKeyFrames",
        "ObjectAnimationUsingKeyFrames",
        "DiscreteObjectKeyFrame",
        "DiscreteDoubleKeyFrame",
        "LinearDoubleKeyFrame",
        "EasingDoubleKeyFrame",
        "SplineDoubleKeyFrame",
        "LinearColorKeyFrame",
        "DiscreteColorKeyFrame",
        "EasingColorKeyFrame",
        "SplineColorKeyFrame",
        "QuadraticEase",
        "CubicEase",
        "QuarticEase",
        "QuinticEase",
        "SineEase",
        "ExponentialEase",
        "CircleEase",
        "BackEase",
        "BounceEase",
        "ElasticEase",
        "PowerEase",
        "RepositionThemeAnimation",
        "PointerUpThemeAnimation",
        "PointerDownThemeAnimation",
        "FadeInThemeAnimation",
        "FadeOutThemeAnimation",
        "TransitionCollection",
        "EntranceThemeTransition",
        "ContentThemeTransition",
        "AddDeleteThemeTransition",
        "RepositionThemeTransition",
        // Attached property owners
        "AutomationProperties",
        "ToolTipService",
        "Control",
        "FrameworkElement",
        "UIElement",
        // Transforms
        "TranslateTransform",
        "ScaleTransform",
        "RotateTransform",
        "SkewTransform",
        "CompositeTransform",
        "TransformGroup",
    };
    static const std::set<std::string_view> intrinsic = {"Boolean", "Double", "Int32",
                                                         "Int64",   "String", "Object"};
    if (name.ns == kXamlPresentationNs)
        return presentation.contains(name.local);
    if (name.ns == kXamlNs)
        return intrinsic.contains(name.local);
    return name.ns.starts_with("using:");
}

} // namespace velora
