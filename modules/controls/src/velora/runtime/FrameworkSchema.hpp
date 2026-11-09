#pragma once

#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <velora/globals.hpp>

// The XAML-visible surface of the built-in UI framework: element types, properties and
// events. It is the single source for both the runtime (velora/runtime/Framework.hpp creates a
// descriptor for every entry) and the XAML compiler (velorac checks members and values
// against it), so the two cannot drift apart.
namespace velora
{

enum class ValueKind : u8
{
    kString,
    kDouble, // "Auto" (and an empty default) means unset
    kInteger,
    kBoolean,
    kThickness,    // "4", "8,4", "1,2,3,4"
    kBrush,        // "#AARRGGBB", "#RRGGBB", "#ARGB", "#RGB" or a named color; stored as 0xAARRGGBB
    kGridLengths,  // set through RowDefinition/ColumnDefinition property elements
    kEnum,         // one of `choices`
    kVisibility,   // Visible | Collapsed, stored as the IsVisible boolean
    kObject,       // application object (ObjectRef); set through x:Bind or {Binding} only
    kCornerRadius, // "4" or "4,4,4,4"; only uniform radii are supported, stored as a double
};

// Flag bits match PropertyFlags; Inherited makes the value flow to descendants.
inline constexpr unsigned kSchemaMeasure = 1, kSchemaArrange = 2, kSchemaPaint = 4, kSchemaInherited = 8;

// X(member, "XAML name", kind, "choices", "default", flags)
#define VELORA_UI_PROPERTIES(X)                                                                              \
    X(Width, "Width", Double, "", "", kSchemaMeasure)                                                        \
    X(Height, "Height", Double, "", "", kSchemaMeasure)                                                      \
    X(MinWidth, "MinWidth", Double, "", "0", kSchemaMeasure)                                                 \
    X(MinHeight, "MinHeight", Double, "", "0", kSchemaMeasure)                                               \
    X(Margin, "Margin", Thickness, "", "0", kSchemaMeasure)                                                  \
    X(Padding, "Padding", Thickness, "", "0", kSchemaMeasure)                                                \
    X(HorizontalAlignment, "HorizontalAlignment", Enum, "Left|Center|Right|Stretch", "", kSchemaArrange)     \
    X(VerticalAlignment, "VerticalAlignment", Enum, "Top|Center|Bottom|Stretch", "", kSchemaArrange)         \
    X(Opacity, "Opacity", Double, "", "1", kSchemaPaint)                                                     \
    X(Visibility, "Visibility", Visibility, "Visible|Collapsed", "Visible", kSchemaMeasure)                  \
    X(Background, "Background", Brush, "", "", kSchemaPaint)                                                 \
    X(Foreground, "Foreground", Brush, "", "#E4000000", kSchemaPaint)                                        \
    X(BorderBrush, "BorderBrush", Brush, "", "", kSchemaPaint)                                               \
    X(BorderThickness, "BorderThickness", Thickness, "", "0", kSchemaMeasure)                                \
    X(CornerRadius, "CornerRadius", CornerRadius, "", "0", kSchemaPaint)                                     \
    X(FontSize, "FontSize", Double, "", "14", kSchemaMeasure)                                                \
    X(Text, "Text", String, "", "", kSchemaMeasure)                                                          \
    X(PlaceholderText, "PlaceholderText", String, "", "", kSchemaPaint)                                      \
    X(Stretch, "Stretch", Enum, "Uniform|UniformToFill|Fill", "Uniform", kSchemaPaint)                       \
    X(Content, "Content", String, "", "", kSchemaMeasure)                                                    \
    X(Spacing, "Spacing", Double, "", "0", kSchemaMeasure)                                                   \
    X(Orientation, "Orientation", Enum, "Vertical|Horizontal", "Vertical", kSchemaMeasure)                   \
    X(RowSpacing, "RowSpacing", Double, "", "0", kSchemaMeasure)                                             \
    X(ColumnSpacing, "ColumnSpacing", Double, "", "0", kSchemaMeasure)                                       \
    X(RowDefinitions, "RowDefinitions", GridLengths, "", "", kSchemaMeasure)                                 \
    X(ColumnDefinitions, "ColumnDefinitions", GridLengths, "", "", kSchemaMeasure)                           \
    X(GridRow, "Grid.Row", Integer, "", "0", kSchemaArrange)                                                 \
    X(GridColumn, "Grid.Column", Integer, "", "0", kSchemaArrange)                                           \
    X(GridRowSpan, "Grid.RowSpan", Integer, "", "1", kSchemaArrange)                                         \
    X(GridColumnSpan, "Grid.ColumnSpan", Integer, "", "1", kSchemaArrange)                                   \
    X(IsEnabled, "IsEnabled", Boolean, "", "true", kSchemaPaint)                                             \
    X(Source, "Source", String, "", "", kSchemaMeasure)                                                      \
    X(DataContext, "DataContext", Object, "", "", kSchemaInherited)                                          \
    X(IsTabStop, "IsTabStop", Boolean, "", "true", 0)                                                        \
    X(TabIndex, "TabIndex", Integer, "", "0", 0)                                                             \
    X(ItemHeight, "ItemHeight", Double, "", "40", kSchemaMeasure)                                            \
    X(HorizontalScrollBarVisibility, "HorizontalScrollBarVisibility", Enum, "Disabled|Auto|Hidden|Visible",  \
      "Disabled", kSchemaMeasure)                                                                            \
    X(VerticalScrollBarVisibility, "VerticalScrollBarVisibility", Enum, "Disabled|Auto|Hidden|Visible",      \
      "Visible", kSchemaMeasure)                                                                             \
    X(IsChecked, "IsChecked", Boolean, "", "false", kSchemaPaint)                                            \
    X(GroupName, "GroupName", String, "", "", kSchemaPaint)                                                  \
    X(IsOn, "IsOn", Boolean, "", "false", kSchemaPaint)                                                      \
    X(Header, "Header", String, "", "", kSchemaMeasure)                                                      \
    X(OnContent, "OnContent", String, "", "On", kSchemaMeasure)                                              \
    X(OffContent, "OffContent", String, "", "Off", kSchemaMeasure)                                           \
    X(Value, "Value", Double, "", "0", kSchemaPaint)                                                         \
    X(Minimum, "Minimum", Double, "", "0", kSchemaPaint)                                                     \
    X(Maximum, "Maximum", Double, "", "100", kSchemaPaint)                                                   \
    X(StepFrequency, "StepFrequency", Double, "", "1", kSchemaPaint)                                         \
    X(CanvasLeft, "Canvas.Left", Double, "", "0", kSchemaArrange)                                            \
    X(CanvasTop, "Canvas.Top", Double, "", "0", kSchemaArrange)

// X(member, "XAML name", "C++ handler signature")
#define VELORA_UI_EVENTS(X)                                                                                  \
    X(Click, "Click", "void()")                                                                              \
    X(TextChanged, "TextChanged", "void()")                                                                  \
    X(ViewChanged, "ViewChanged", "void()")                                                                  \
    X(Checked, "Checked", "void()")                                                                          \
    X(Unchecked, "Unchecked", "void()")                                                                      \
    X(Toggled, "Toggled", "void()")                                                                          \
    X(ValueChanged, "ValueChanged", "void()")

struct SchemaProperty
{
    std::string_view name;
    ValueKind kind;
    std::string_view choices;      // Enum and Visibility: '|'-separated
    std::string_view defaultValue; // XAML spelling; empty means unset
    unsigned flags;
};

struct SchemaEvent
{
    std::string_view name, signature;
};

inline constexpr SchemaProperty kSchemaProperties[] = {
#define VELORA_SCHEMA_PROPERTY(member, name, kind, choices, value, flags)                                    \
    {name, ValueKind::k##kind, choices, value, flags},
    VELORA_UI_PROPERTIES(VELORA_SCHEMA_PROPERTY)
#undef VELORA_SCHEMA_PROPERTY
};

inline constexpr SchemaEvent kSchemaEvents[] = {
#define VELORA_SCHEMA_EVENT(member, name, signature) {name, signature},
    VELORA_UI_EVENTS(VELORA_SCHEMA_EVENT)
#undef VELORA_SCHEMA_EVENT
};

// What an element's child content becomes.
enum class ContentModel : u8
{
    kNone,
    kText,     // text content sets `contentProperty`
    kSingle,   // exactly one child element
    kChildren, // any number of child elements
};

struct SchemaDefault
{
    std::string_view property, value; // per-type defaults, applied at the Style level
};

struct SchemaType
{
    std::string_view name;
    ContentModel content;
    std::string_view contentProperty;             // ContentModel::kText: the property the text sets
    std::span<const std::string_view> properties; // in addition to kCommonProperties
    std::span<const std::string_view> events;
    std::span<const SchemaDefault> defaults;
    // Items host: ItemsSource (x:Bind to an ObservableVector) and an ItemTemplate DataTemplate,
    // realized and recycled for the visible range only.
    bool items = false;
};

// Layout and visual properties every element accepts.
inline constexpr std::string_view kCommonProperties[] = {
    "Width",
    "Height",
    "MinWidth",
    "MinHeight",
    "Margin",
    "HorizontalAlignment",
    "VerticalAlignment",
    "Opacity",
    "Visibility",
    "Grid.Row",
    "Grid.Column",
    "Grid.RowSpan",
    "Grid.ColumnSpan",
    "Canvas.Left",
    "Canvas.Top",
    "DataContext",
    "IsTabStop",
    "TabIndex",
};
inline constexpr std::string_view kSchemaRootProperties[] = {"Background", "Padding"};
inline constexpr std::string_view kSchemaPanelProperties[] = {
    "Background", "Padding", "BorderBrush", "BorderThickness", "CornerRadius", "Orientation", "Spacing"};
inline constexpr std::string_view kSchemaGridProperties[] = {
    "Background",     "Padding",           "BorderBrush", "BorderThickness", "CornerRadius",
    "RowDefinitions", "ColumnDefinitions", "RowSpacing",  "ColumnSpacing"};
inline constexpr std::string_view kSchemaBorderProperties[] = {"Background", "BorderBrush", "BorderThickness",
                                                               "CornerRadius", "Padding"};
inline constexpr std::string_view kSchemaTextBlockProperties[] = {"Text", "FontSize", "Foreground"};
inline constexpr std::string_view kSchemaTextBoxProperties[] = {
    "Text",        "PlaceholderText", "FontSize",     "Foreground", "Background",
    "BorderBrush", "BorderThickness", "CornerRadius", "Padding",    "IsEnabled"};
inline constexpr std::string_view kSchemaButtonProperties[] = {
    "Content",         "FontSize",     "Foreground", "Background", "BorderBrush",
    "BorderThickness", "CornerRadius", "Padding",    "IsEnabled"};
inline constexpr std::string_view kSchemaImageProperties[] = {"Source", "Stretch"};
inline constexpr std::string_view kSchemaContentPresenterProperties[] = {
    "Content",     "FontSize",        "Foreground",   "Background",
    "BorderBrush", "BorderThickness", "CornerRadius", "Padding"};
inline constexpr std::string_view kSchemaListViewProperties[] = {
    "ItemHeight", "Background", "BorderBrush", "BorderThickness", "CornerRadius", "Padding"};
// ScrollViewer: a ScrollBarVisibility of Disabled constrains that axis to the viewport; the
// other values make it scrollable (Hidden shows no indicator, Auto only while it can scroll).
inline constexpr std::string_view kSchemaScrollViewerProperties[] = {"Background",
                                                                     "Padding",
                                                                     "BorderBrush",
                                                                     "BorderThickness",
                                                                     "CornerRadius",
                                                                     "IsEnabled",
                                                                     "HorizontalScrollBarVisibility",
                                                                     "VerticalScrollBarVisibility"};
// Toggle and range controls (WinUI CheckBox, RadioButton, ToggleSwitch, Slider, ProgressBar).
inline constexpr std::string_view kSchemaCheckBoxProperties[] = {"Content", "FontSize",  "Foreground",
                                                                 "Padding", "IsEnabled", "IsChecked"};
inline constexpr std::string_view kSchemaRadioButtonProperties[] = {
    "Content", "FontSize", "Foreground", "Padding", "IsEnabled", "IsChecked", "GroupName"};
inline constexpr std::string_view kSchemaToggleSwitchProperties[] = {
    "Header", "OnContent", "OffContent", "IsOn", "FontSize", "Foreground", "IsEnabled"};
inline constexpr std::string_view kSchemaSliderProperties[] = {"Value", "Minimum", "Maximum", "StepFrequency",
                                                               "IsEnabled"};
inline constexpr std::string_view kSchemaProgressBarProperties[] = {"Value", "Minimum", "Maximum",
                                                                    "Foreground", "Background"};
inline constexpr std::string_view kSchemaCanvasProperties[] = {"Background"};
inline constexpr std::string_view kSchemaToggleEvents[] = {"Click", "Checked", "Unchecked"};
inline constexpr std::string_view kSchemaToggled[] = {"Toggled"};
inline constexpr std::string_view kSchemaValueChanged[] = {"ValueChanged"};
inline constexpr SchemaDefault kSchemaToggleDefaults[] = {
    {"HorizontalAlignment", "Left"}, {"VerticalAlignment", "Center"}, {"MinHeight", "32"}};
inline constexpr SchemaDefault kSchemaToggleSwitchDefaults[] = {{"HorizontalAlignment", "Left"},
                                                                {"VerticalAlignment", "Center"}};
inline constexpr SchemaDefault kSchemaSliderDefaults[] = {{"VerticalAlignment", "Center"},
                                                          {"MinHeight", "32"}};
inline constexpr SchemaDefault kSchemaProgressBarDefaults[] = {{"VerticalAlignment", "Center"},
                                                               {"MinHeight", "4"},
                                                               {"Foreground", "#FF0078D4"},
                                                               {"Background", "#33000000"}};
inline constexpr std::string_view kSchemaClick[] = {"Click"};
inline constexpr std::string_view kSchemaTextChanged[] = {"TextChanged"};
inline constexpr std::string_view kSchemaViewChanged[] = {"ViewChanged"};
inline constexpr SchemaDefault kSchemaButtonDefaults[] = {
    {"HorizontalAlignment", "Left"}, {"VerticalAlignment", "Center"}, {"Padding", "11,5,11,6"},
    {"CornerRadius", "4"},           {"Background", "#FFF3F3F3"},     {"BorderBrush", "#FFD1D1D1"},
    {"BorderThickness", "1"}};
inline constexpr SchemaDefault kSchemaTextBoxDefaults[] = {
    {"VerticalAlignment", "Center"}, {"MinWidth", "64"},          {"Padding", "10,5,6,6"},
    {"CornerRadius", "4"},           {"Background", "#FFFFFFFF"}, {"BorderBrush", "#FF8A8A8A"},
    {"BorderThickness", "1"}};

inline constexpr SchemaType kSchemaTypes[] = {
    {"Page", ContentModel::kSingle, "", kSchemaRootProperties, {}, {}},
    {"UserControl", ContentModel::kSingle, "", kSchemaRootProperties, {}, {}},
    {"Grid", ContentModel::kChildren, "", kSchemaGridProperties, {}, {}},
    {"StackPanel", ContentModel::kChildren, "", kSchemaPanelProperties, {}, {}},
    {"Border", ContentModel::kSingle, "", kSchemaBorderProperties, {}, {}},
    {"TextBlock", ContentModel::kText, "Text", kSchemaTextBlockProperties, {}, {}},
    {"TextBox", ContentModel::kNone, "", kSchemaTextBoxProperties, kSchemaTextChanged,
     kSchemaTextBoxDefaults},
    {"Button", ContentModel::kText, "Content", kSchemaButtonProperties, kSchemaClick, kSchemaButtonDefaults},
    {"Image", ContentModel::kNone, "", kSchemaImageProperties, {}, {}},
    {"ListView", ContentModel::kNone, "", kSchemaListViewProperties, {}, {}, true},
    // Shows the templated parent's Content inside a ControlTemplate.
    {"ContentPresenter", ContentModel::kNone, "", kSchemaContentPresenterProperties, {}, {}},
    // ViewChanged: raised after a layout that moved the offsets (no intermediate views).
    {"ScrollViewer", ContentModel::kSingle, "", kSchemaScrollViewerProperties, kSchemaViewChanged, {}},
    {"CheckBox", ContentModel::kText, "Content", kSchemaCheckBoxProperties, kSchemaToggleEvents,
     kSchemaToggleDefaults},
    {"RadioButton", ContentModel::kText, "Content", kSchemaRadioButtonProperties, kSchemaToggleEvents,
     kSchemaToggleDefaults},
    {"ToggleSwitch", ContentModel::kNone, "", kSchemaToggleSwitchProperties, kSchemaToggled,
     kSchemaToggleSwitchDefaults},
    {"Slider", ContentModel::kNone, "", kSchemaSliderProperties, kSchemaValueChanged, kSchemaSliderDefaults},
    {"ProgressBar", ContentModel::kNone, "", kSchemaProgressBarProperties, {}, kSchemaProgressBarDefaults},
    // Children are placed at their Canvas.Left/Canvas.Top with their desired size.
    {"Canvas", ContentModel::kChildren, "", kSchemaCanvasProperties, {}, {}},
};

// Built-in element kind: its index in kSchemaTypes. C++ factories and generated XAML use it
// to select the element's schema and construction behavior without a type-name lookup.
enum class ElementKind : u8
{
    kPage,
    kUserControl,
    kGrid,
    kStackPanel,
    kBorder,
    kTextBlock,
    kTextBox,
    kButton,
    kImage,
    kListView,
    kContentPresenter,
    kScrollViewer,
    kCheckBox,
    kRadioButton,
    kToggleSwitch,
    kSlider,
    kProgressBar,
    kCanvas,
};

// Element id tagged with its built-in kind for generated x:Name fields. Explicit construction
// records a caller-known kind; conversion supplies the untagged id used by Framework APIs.
// The tag does not validate tree membership or keep the element alive.
template <ElementKind Kind> struct TypedElementId
{
    u64 value = 0;
    constexpr TypedElementId() noexcept = default;
    constexpr explicit TypedElementId(u64 id) noexcept : value(id)
    {
    }
    constexpr operator u64() const noexcept
    {
        return value;
    }
};

constexpr const SchemaType& schemaType(ElementKind kind)
{
    return kSchemaTypes[static_cast<usize>(kind)];
}

static_assert(std::size(kSchemaTypes) == static_cast<usize>(ElementKind::kCanvas) + 1);
static_assert(schemaType(ElementKind::kPage).name == "Page" &&
              schemaType(ElementKind::kUserControl).name == "UserControl" &&
              schemaType(ElementKind::kGrid).name == "Grid" &&
              schemaType(ElementKind::kStackPanel).name == "StackPanel" &&
              schemaType(ElementKind::kBorder).name == "Border" &&
              schemaType(ElementKind::kTextBlock).name == "TextBlock" &&
              schemaType(ElementKind::kTextBox).name == "TextBox" &&
              schemaType(ElementKind::kButton).name == "Button" &&
              schemaType(ElementKind::kImage).name == "Image" &&
              schemaType(ElementKind::kListView).name == "ListView" &&
              schemaType(ElementKind::kContentPresenter).name == "ContentPresenter" &&
              schemaType(ElementKind::kScrollViewer).name == "ScrollViewer" &&
              schemaType(ElementKind::kCheckBox).name == "CheckBox" &&
              schemaType(ElementKind::kRadioButton).name == "RadioButton" &&
              schemaType(ElementKind::kToggleSwitch).name == "ToggleSwitch" &&
              schemaType(ElementKind::kSlider).name == "Slider" &&
              schemaType(ElementKind::kProgressBar).name == "ProgressBar" &&
              schemaType(ElementKind::kCanvas).name == "Canvas");

constexpr const SchemaProperty* findSchemaProperty(std::string_view name)
{
    for (const auto& p : kSchemaProperties)
        if (p.name == name)
            return &p;
    return nullptr;
}

constexpr const SchemaEvent* findSchemaEvent(std::string_view name)
{
    for (const auto& e : kSchemaEvents)
        if (e.name == name)
            return &e;
    return nullptr;
}

constexpr const SchemaType* findSchemaType(std::string_view name)
{
    for (const auto& t : kSchemaTypes)
        if (t.name == name)
            return &t;
    return nullptr;
}

constexpr std::optional<ElementKind> findElementKind(std::string_view name)
{
    for (usize i = 0; i < std::size(kSchemaTypes); ++i)
        if (kSchemaTypes[i].name == name)
            return static_cast<ElementKind>(i);
    return std::nullopt;
}

constexpr bool hasSchemaProperty(const SchemaType& type, std::string_view name)
{
    for (auto p : kCommonProperties)
        if (p == name)
            return true;
    for (auto p : type.properties)
        if (p == name)
            return true;
    return false;
}

constexpr bool hasSchemaEvent(const SchemaType& type, std::string_view name)
{
    for (auto e : type.events)
        if (e == name)
            return true;
    return false;
}

} // namespace velora
