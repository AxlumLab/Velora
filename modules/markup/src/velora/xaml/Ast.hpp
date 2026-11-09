#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <velora/globals.hpp>

// XAML syntax tree produced by velora::parse. Build-time only: the XAML compiler
// lowers it to C++; applications never parse XML at runtime.
namespace velora
{

// Line and column are 1-based; the column counts Unicode code points. offset is the byte
// offset into the UTF-8 source (after a byte order mark, if any).
struct Position
{
    u32 line = 0, column = 0, offset = 0;
    friend bool operator==(const Position&, const Position&) = default;
};
struct Span
{
    Position begin, end; // end is exclusive
    friend bool operator==(const Span&, const Span&) = default;
};

// XAML namespace URIs.
inline constexpr std::string_view kXamlPresentationNs =
    "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
inline constexpr std::string_view kXamlNs = "http://schemas.microsoft.com/winfx/2006/xaml";
inline constexpr std::string_view kXamlDesignNs = "http://schemas.microsoft.com/expression/blend/2008";
inline constexpr std::string_view kXamlCompatibilityNs =
    "http://schemas.openxmlformats.org/markup-compatibility/2006";
inline constexpr std::string_view kXmlNs = "http://www.w3.org/XML/1998/namespace";

struct QName
{
    std::string prefix, local;
    std::string ns;        // resolved namespace URI without a conditional suffix
    std::string condition; // conditional XAML predicate, e.g. IsApiContractPresent(...)
};

// ---- x:Bind -------------------------------------------------------------------------

struct BindStep
{
    enum class Kind
    {
        kMember,
        kIndex
    } kind = Kind::kMember;
    std::string name; // member name, or the index literal spelling
    Span span;
};
struct BindPath
{
    std::vector<BindStep> steps;
    Span span;
};
struct BindLiteral
{
    enum class Kind
    {
        kInteger,
        kFloat,
        kString,
        kBoolean,
        kNull
    } kind = Kind::kInteger;
    std::string text; // strings unquoted and unescaped
    Span span;
};
struct BindExpression
{
    BindPath path; // the bound path, or the function path of a call
    bool call = false;
    std::vector<std::variant<BindPath, BindLiteral>> arguments;
    Span span;
};

// ---- Markup extensions --------------------------------------------------------------

struct MarkupExtension;
struct MarkupValue
{
    std::string text;                                 // scalar value with escapes removed and quotes stripped
    std::shared_ptr<const MarkupExtension> extension; // set when the value is a nested extension
    Span span;
};
struct MarkupArgument
{
    std::string name; // empty for the positional argument
    Span nameSpan;
    MarkupValue value;
};
struct MarkupExtension
{
    QName name;       // e.g. {x:Bind} -> prefix "x", local "Bind"
    std::string kind; // canonical: StaticResource, ThemeResource, Binding, TemplateBinding, x:Bind, x:Null,
                      // RelativeSource
    std::optional<MarkupArgument> positional;
    std::vector<MarkupArgument> named;
    std::optional<BindExpression> bind; // x:Bind path or call
    Span span;

    const MarkupArgument* argument(std::string_view name) const;
};

// ---- XML ------------------------------------------------------------------------------

enum class XamlAttributeKind
{
    kProperty,             // Width="10"
    kAttachedProperty,     // Grid.Row="0"
    kDirective,            // x:Name, x:Key, x:Class, ...
    kNamespaceDeclaration, // xmlns, xmlns:p
    kXmlAttribute,         // xml:space, xml:lang
    kIgnored,              // in an mc:Ignorable namespace (design-time data)
};

struct XamlAttribute
{
    QName name;
    XamlAttributeKind kind = XamlAttributeKind::kProperty;
    std::string owner; // AttachedProperty: owning type
    std::string raw;   // value as written, without quotes
    std::string value; // entities decoded, attribute whitespace normalized, {} escape removed
    std::shared_ptr<const MarkupExtension> markup;
    Span span, valueSpan;
};

struct XamlElement;
struct XamlText
{
    std::string raw;   // as written (CDATA without its delimiters)
    std::string value; // decoded; whitespace collapsed unless xml:space="preserve" or CDATA
    bool cdata = false;
    Span span;
};
struct XamlComment
{
    std::string text;
    Span span;
};
using XamlNode = std::variant<std::shared_ptr<XamlElement>, XamlText, XamlComment>;

struct XamlElement
{
    QName name;
    bool propertyElement = false; // <Owner.Property>
    std::string owner, property;  // property elements and attached property elements
    bool ignored = false;         // in an mc:Ignorable namespace
    bool preserveSpace = false;
    std::vector<XamlAttribute> attributes;
    std::vector<XamlNode> children; // property elements, object elements (content), text, comments
    Span span, nameSpan;

    const XamlAttribute* attribute(std::string_view ns, std::string_view local) const;
};

struct XamlDocument
{
    std::shared_ptr<XamlElement> root;
    std::vector<XamlComment> comments; // outside the root element
    std::string encoding = "utf-8";
};

struct XamlDiagnostic
{
    Span span;
    std::string message;
};

} // namespace velora
