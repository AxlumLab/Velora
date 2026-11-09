#pragma once

// Internal to veloraXamlCompiler: the compiler state shared by compiler.cpp (elements,
// bindings, code generation) and compiler_resources.cpp (dictionaries, styles, templates,
// visual states).
#include "velora/xaml/Compiler.hpp"
#include "velora/xaml/Parser.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <velora/reflect/Metadata.hpp>
#include <velora/runtime/FrameworkSchema.hpp>
#include <velora/runtime/SchemaValue.hpp>

namespace velora
{

namespace fs = std::filesystem;

struct XamlClassName
{
    std::string ns, name; // C++ namespace (A::B) and class name
    std::string qualified() const
    {
        return ns.empty() ? name : ns + "::" + name;
    }
};

struct XamlView
{
    std::string header; // generated header of the peer
    XamlClassName controller;
    fs::path file;
};

// Themes are resolved at compile time: index 0 is Light, 1 is Dark.
constexpr std::array<const char*, 2> kXamlThemeNames{"Light", "Dark"};

// A value per theme. `themed()` values become ThemeResource updates at run time.
struct ThemedResourceValue
{
    ValueKind kind = ValueKind::kString;
    std::array<PropertyValue, 2> values;
    bool themed() const
    {
        return !(values[0] == values[1]);
    }
};

struct XamlDictionary;
struct XamlResource
{
    std::string key;
    const XamlElement* element = nullptr;
    enum class Type
    {
        kValue,
        kAlias,
        kStyle,
        kTemplate
    } type = Type::kValue;
    ValueKind kind = ValueKind::kString; // Value
    XamlDictionary* home = nullptr;      // where its own references resolve
    enum class State
    {
        kPending,
        kResolving,
        kDone,
        kFailed
    };
    std::array<State, 2> state{State::kPending, State::kPending};
    std::array<PropertyValue, 2> value;
    std::array<ValueKind, 2> resolvedKind{};
};

// A ResourceDictionary: its entries, then MergedDictionaries (last first), then the
// ThemeDictionaries entry for the theme (or "Default"); lookups continue in `parent`.
struct XamlDictionary
{
    std::map<std::string, XamlResource, std::less<>> entries;
    std::vector<XamlDictionary*> merged;
    std::map<std::string, XamlDictionary*, std::less<>> themes;
    XamlDictionary* parent = nullptr;
    std::string file; // set for dictionaries loaded from another XAML file
};

struct XamlStyle
{
    std::string target;
    struct Setter
    {
        std::string property;
        const XamlElement* element;
        XamlDictionary* home;
    };
    std::vector<Setter> setters; // base first; a derived setter replaces the base one
    const XamlElement* templateElement = nullptr;
    XamlDictionary* templateHome = nullptr;
};

// A OneWay or TwoWay x:Bind: `apply` writes the target from the source; one subscription per
// path step (owners[i] owns property names[i]) re-applies it when that step changes, and a
// change of an intermediate step re-subscribes the steps after it.
struct XamlLiveBinding
{
    std::string apply;
    std::vector<std::string> owners, names;
    bool guarded = false; // TwoWay: ignores source changes raised while writing back
};

// One generated class: the page view, a DataTemplate container (ItemTemplate<n>) or a
// ControlTemplate instance (ControlTemplate<n>).
struct XamlScope
{
    enum class Kind
    {
        kView,
        kItemTemplate,
        kControlTemplate
    } kind = Kind::kView;
    std::string cls;
    std::string rootType; // x:Bind root: the controller, or the template's x:DataType
    std::string rootExpr; // "m_controller" or "(*m_item)"
    std::size_t elements = 0;
    std::vector<std::string> build, once, unbind, refresh, states;
    std::vector<XamlLiveBinding> live;
    std::map<std::pair<std::size_t, std::string>, std::vector<std::string>> handlers;
    std::map<std::string, std::pair<Span, std::size_t>> names; // x:Name -> (span, element index)
    std::vector<std::string> nameOrder;
    std::map<std::string, std::string> nameTypes; // x:Name -> element type, for state targets
    std::vector<std::string> children;            // custom control controller types
    struct Repeater
    {
        std::string item, tmpl, name; // item type, template class, ListView x:Name (accessor) or empty
    };
    std::vector<Repeater> repeaters;
    std::vector<std::string> controlTemplates; // template<k>_ member classes
    const XamlElement* visualStateGroups = nullptr;
    bool isItemTemplate() const
    {
        return kind == Kind::kItemTemplate;
    }
    bool isControlTemplate() const
    {
        return kind == Kind::kControlTemplate;
    }
};

enum class TypeCategory
{
    kString,
    kInteger,
    kFloating,
    kBoolean,
    kOther
};

class XamlCompiler
{
public:
    // Shared helpers of the compiler's translation units.
    static std::string readFile(const fs::path& path);
    static std::string cppString(std::string_view s);    // C++ string literal
    static std::string cppValue(const PropertyValue& v); // C++ expression constructing the value
    static std::string memberOf(std::string_view property);
    static std::string bareType(std::string_view type);
    static bool identifier(std::string_view s);
    static bool dottedPath(std::string_view s);
    static bool isPointer(std::string_view type);
    static bool isMutableReference(std::string_view type);
    static TypeCategory category(std::string_view type);
    static std::optional<std::string> convert(const std::string& expr, std::string_view type, ValueKind kind);
    static std::optional<std::string> vectorItem(std::string_view type);
    static XamlClassName className(std::string_view dotted);
    static void sortDiagnostics(std::vector<XamlDiagnostic>& diagnostics);
    static bool presentation(const XamlElement& e, std::string_view local);
    static std::vector<const XamlElement*> objectChildren(const XamlElement& e);
    static std::string textOf(const XamlElement& e);
    static std::string targetProperty(std::string path);
    static bool numeric(ValueKind kind);
    static const MarkupArgument& resourceKey(const MarkupExtension& m);

    XamlCompiler(const fs::path& path, const CompileOptions& options, CompileResult& result)
        : m_path(path), m_options(options), m_result(result)
    {
    }

    void run(const XamlDocument& document);
    // `velorac resources`: loads a ResourceDictionary file and resolves every entry in both themes.
    void checkDictionary(const XamlDocument& document);

private:
    void error(const Span& span, std::string message)
    {
        m_result.diagnostics.push_back({span, std::move(message)});
    }
    // Reports a problem found in `home`'s file at the place in this file that led there.
    void errorIn(const XamlDictionary* home, const Span& span, const Span& reference,
                 const std::string& message);
    std::string line(const Span& span) const
    {
        return "#line " + std::to_string(span.begin.line) + " " + cppString(m_path.generic_string()) + "\n";
    }
    void loadMetadataAndPeers(); // also builds the application dictionary
    void useType(std::string_view qualified);
    std::string elementExpr(std::size_t index) const
    {
        return "m_elements[" + std::to_string(index) + "]";
    }

    // compiler.cpp
    void element(const XamlElement& e, const std::string& parentExpr);
    void customElement(const XamlElement& e, std::size_t index, const TypeMetadata& type,
                       const std::string& parentExpr);
    void items(const XamlElement& e, std::size_t index, const XamlAttribute* source,
               const XamlElement* itemTemplate);
    void setProperty(std::size_t index, const SchemaType* type, const XamlAttribute& a,
                     std::set<std::string>& assigned);
    void bind(std::size_t index, const SchemaType* type, const SchemaProperty& property,
              const XamlAttribute& a);
    void binding(std::size_t index, const SchemaType* type, const SchemaProperty& property,
                 const MarkupExtension& m);
    struct BoundPath
    {
        std::string getterExpr, type;
        std::vector<BindingStep> steps;
        std::vector<std::string> owners; // expression of the object owning each step
    };
    std::optional<BoundPath> boundPath(const MarkupExtension& m, bool twoWay);
    // Adds a binding that runs `apply` once (OneTime) or keeps it current along `path`.
    void addBinding(const std::string& mode, const BoundPath& path, std::string apply, bool guarded = false);
    void handler(std::size_t index, std::string_view event, const XamlAttribute& a);
    void propertyElement(std::size_t index, const SchemaType& type, const XamlElement& p,
                         std::set<std::string>& assigned);
    std::string resolveTypeName(std::string_view prefixed);
    void emit();
    void emitClass(const XamlScope& scope, std::ostringstream& h, std::ostringstream& s,
                   const std::string& qualified);

    // compiler_resources.cpp
    XamlDictionary* newDictionary(XamlDictionary* parent, std::string file = {});
    // Fills `d` from a ResourceDictionary element (or a Resources property element's content).
    void loadDictionary(const XamlElement& e, XamlDictionary* d);
    XamlDictionary* dictionaryFile(const std::string& source, const Span& reference);
    void publishResources(const XamlElement& e, std::size_t index);
    bool elementResources(const XamlElement& e); // pushes a dictionary for e's Resources
    XamlResource* findIn(XamlDictionary* d, std::string_view key, int theme);
    XamlResource* lookup(std::string_view key, XamlDictionary* d,
                         int theme); // then the application dictionary
    std::optional<ThemedResourceValue> resourceValue(const MarkupExtension& m, XamlDictionary* scope,
                                                     ValueKind wanted, std::string_view property);
    std::optional<PropertyValue> resolve(XamlResource& r, int theme, const Span& reference,
                                         std::vector<std::string>& chain);
    // Value of an attribute (literal, StaticResource/ThemeResource) or of inline brush content.
    std::optional<ThemedResourceValue> literalOrResource(const XamlAttribute& a, XamlDictionary* scope,
                                                         const SchemaProperty& property);
    void setValue(std::size_t index, std::string_view property, const ThemedResourceValue& v, bool live,
                  const char* source);
    const XamlStyle* style(XamlResource& r, const Span& reference, std::vector<std::string>& chain);
    std::optional<XamlStyle> inlineStyle(const XamlElement& style, XamlDictionary* home,
                                         const Span& reference, std::vector<std::string>& chain);
    // Applies the explicit (Style attribute or property element) or implicit style; returns its template, if
    // any.
    std::pair<const XamlElement*, XamlDictionary*> applyStyle(const XamlElement& e, std::size_t index,
                                                              const SchemaType& type);
    void instantiateTemplate(const XamlElement& owner, std::size_t index, const SchemaType& type,
                             const XamlElement& controlTemplate, XamlDictionary* home);
    void templateBinding(std::size_t index, const SchemaType& type, const SchemaProperty& property,
                         const MarkupExtension& m);
    void visualStates(const XamlElement& groups);
    std::optional<double> timespan(const XamlAttribute* a);

    const fs::path& m_path;
    const CompileOptions& m_options;
    CompileResult& m_result;
    MetadataCatalog m_catalog;
    std::map<std::string, std::pair<std::string, fs::path>, std::less<>>
        m_typeFiles; // type -> (include, rkmeta)
    std::map<std::string, XamlView, std::less<>> m_views;
    struct DictionaryFile
    {
        fs::path path;
        std::optional<XamlParseResult> parsed;
        XamlDictionary* dictionary = nullptr;
        bool loading = false;
    };
    std::map<std::string, DictionaryFile, std::less<>> m_dictionaryFiles; // by file name
    std::set<std::string> m_includes, m_viewIncludes;
    std::set<fs::path> m_dependencies;
    std::map<std::string, std::string> m_prefixes; // xmlns prefix -> URI, from the root element
    XamlClassName m_class;
    const TypeMetadata* m_controller = nullptr;
    XamlScope m_page;
    std::vector<std::unique_ptr<XamlScope>> m_templates; // item and control templates, in creation order
    std::map<const XamlElement*, std::string> m_controlTemplateClasses;
    XamlScope* m_scope = &m_page;
    std::vector<std::unique_ptr<XamlDictionary>> m_dictionaryStore;
    XamlDictionary* m_dictionary = nullptr;  // innermost dictionary for the element being compiled
    XamlDictionary* m_application = nullptr; // application-level resources, parent of every scope
    std::map<const XamlResource*, std::optional<XamlStyle>> m_styles;
};

} // namespace velora
