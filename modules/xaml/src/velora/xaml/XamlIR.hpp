#pragma once

#include <string>
#include <vector>
#include <velora/globals.hpp>
#include <velora/types/TypeId.hpp>
#include <velora/runtime/property/PropertyTypes.hpp>

// XamlIR (R06.1): the compiled form of one XAML file — what velorac writes into a .vxbf and what
// the XamlLoader (R07) replays. Declarative only: object types, property values, bindings,
// resource references, names and event connections. The serialized file contains no C++ addresses,
// STL layouts, runtime ElementIds or direct tree-building calls.
//
// Instructions form a pre-order walk: kBeginObject creates an object as a child of the enclosing
// one (the first object is the root), the instructions up to its kEndObject configure it.
//
// Types and properties are referenced by stable ids derived from their names (typeIdFor,
// propertyIdFor); the names are kept for diagnostics and for readers to report what is missing.

namespace velora
{

struct XamlIRType
{
    TypeId id;
    std::string className; // "Velora.UI.Button"
};

struct XamlIRProperty
{
    u64 id = 0;       // propertyIdFor of the runtime property the XAML name maps to
    std::string name; // the XAML spelling, e.g. "Grid.Row" or "Visibility"
};

// "No string": an absent key (an implicit style) or an absent BasedOn.
constexpr u32 kXamlNoIndex = 0xFFFFFFFFu;
// Objects, dictionaries, styles and templates nested inside each other.
constexpr usize kXamlMaxNesting = 256;

enum class XamlIROp : u8
{
    kBeginObject = 1, // a = type index
    kEndObject = 2,
    kSetValue = 3,    // a = property index, b = value index
    kSetBinding = 4,  // a = property index, b = string index (path), c = BindingMode
    kSetResource = 5, // a = property index, b = string index (key), c = 1 for ThemeResource
    kSetName = 6,     // b = string index (x:Name)
    kAddEvent = 7,    // b = string index (event name), c = string index (handler name)

    // ---- 1.1: resources, styles and templates (R09) ----------------------------------------------
    // The enclosing object's Resources. Holds resource entries only, up to kEndResources.
    kBeginResources = 8,
    kEndResources = 9,
    kResource = 10,             // a = string (key), b = value index
    kResourceReference = 11,    // a = string (key), b = string (referenced key), c = 1 for ThemeResource
    kBeginThemeDictionary = 12, // c = XamlThemeDictionary
    kEndThemeDictionary = 13,
    kMergeDictionary = 14, // b = string (Source, relative to this file)
    kBeginStyle = 15,      // a = string (key) or kXamlNoIndex (implicit), b = target type,
                           // c = string (BasedOn key) or kXamlNoIndex
    kSetter = 16,          // a = property index, b = value index
    kSetterResource = 17,  // a = property index, b = string (key), c = 1 for ThemeResource
    kEndStyle = 18,
    // A deferred ControlTemplate: exactly one object, instantiated per templated control. In
    // resources it has a key (a = string); as a Style's Template setter or an element's Template
    // property it has none (a = kXamlNoIndex). b = target type.
    kBeginTemplate = 19,
    kEndTemplate = 20,
    kSetTemplateBinding = 21, // a = property index, b = property index on the templated parent
    kApplyStyle = 22,         // b = string (key): Style="{StaticResource Key}"
    kSetTemplate = 23,        // b = string (key): Template="{StaticResource Key}"

    // {Binding} options (R09.3): any number of these directly before the kSetBinding they configure.
    kBindElementName = 24,    // b = string (ElementName)
    kBindRelativeSource = 25, // c = XamlRelativeSource
    kBindConverter = 26,      // b = string (converter resource key), c = string (ConverterParameter) or
                              // kXamlNoIndex
    kBindFallback = 27,       // b = value index (FallbackValue)
    kResourceConverter = 28,  // in a dictionary: a = string (key), b = string (registered class name)

    // ---- x:Bind and x:Uid (R09.4, R09.6) ----------------------------------------------------------
    // a = property index, b = the file's x:Bind ordinal (the generated code-behind's accessor for it),
    // c = BindingMode (OneTime is the x:Bind default).
    kSetXBind = 29,
    kSetUid = 30, // b = string (x:Uid): localized property values from the string resources

    // ---- 1.2: VisualStateManager (R10.3) -----------------------------------------------------------
    // VisualStateManager.VisualStateGroups of the enclosing object: groups of states, each with
    // Setters and a Storyboard of From/To animations. Targets are x:Names of the same name scope.
    kBeginVisualStates = 31,
    kEndVisualStates = 32,
    kBeginStateGroup = 33, // b = string (group name)
    kEndStateGroup = 34,
    kBeginState = 35, // b = string (state name)
    kEndState = 36,
    kStateSetter = 37,         // a = property, b = value, c = string (target name)
    kStateSetterResource = 38, // a = property, b = string (key), c = string (target name); StaticResource
    kStateSetterTheme = 39,    // the same for ThemeResource
    // A Double/Color animation of property a on target b (string); c = easing (XamlEasing packed).
    kBeginAnimation = 40,
    kAnimationValue = 41,    // a = XamlAnimationRole, b = value index
    kAnimationResource = 42, // a = XamlAnimationRole | 2 for ThemeResource, b = string (key)
    kAnimationTiming = 43,   // a = Duration in milliseconds, b = BeginTime in milliseconds
    kEndAnimation = 44,
    // In a dictionary, after the kResourceReference of key a: SolidColorBrush.Opacity b (value index,
    // a double) scales the referenced color's alpha.
    kResourceOpacity = 45,
    // Key frames of an animation whose kBeginAnimation has c = kXamlKeyFrames, in time order:
    // a = KeyTime in milliseconds, b = value index (kAnimationKeyFrame) or string key
    // (kAnimationKeyFrameResource), c = XamlKeyFrame interpolation, | kXamlThemeKeyFrame for a
    // ThemeResource value.
    kAnimationKeyFrame = 46,
    kAnimationKeyFrameResource = 47,
    // In a state group: a generated VisualTransition. a = string (From) or kXamlNoIndex,
    // b = string (To) or kXamlNoIndex, c = GeneratedDuration in ms. An optional kTransitionEasing
    // (a = packed easing) follows.
    kStateTransition = 48,
    kTransitionEasing = 49,
    // ---- DataTemplate (R09.5) ----------------------------------------------------------------------
    // A deferred item template: exactly one object, instantiated per realized item with the item as
    // its DataContext. Keyed in a dictionary (a = string), or inline as an ItemTemplate (a = none).
    kBeginDataTemplate = 50,
    kEndDataTemplate = 51,
    kSetItemTemplate = 52, // b = string (key): ItemTemplate="{StaticResource Key}"
    kSetItemsSource = 53,  // b = the file's x:Bind ordinal, c = BindingMode: ItemsSource="{x:Bind ...}"
    // In a style: <Setter Property="Template" Value="{StaticResource Key}"/>; b = string (key).
    kStyleTemplateReference = 54,
    // x:Bind inside a DataTemplate (x:DataType): a = property, b = ordinal of the item accessor,
    // c = BindingMode. Evaluated against the container's item, again whenever the item changes.
    kSetItemXBind = 55,
};

enum class XamlAnimationRole : u32
{
    kFrom = 0,
    kTo = 1,
};

// Easing of a state animation: 0 is linear; otherwise (kind << 8) | mode with kind the Easing::Kind
// (1 Quadratic ... 7 Circle) and mode 0 EaseIn, 1 EaseOut, 2 EaseInOut.
constexpr u32 kXamlLinear = 0;
// A discrete key frame (ObjectAnimationUsingKeyFrames with one DiscreteObjectKeyFrame): the value
// switches at KeyTime instead of interpolating.
constexpr u32 kXamlDiscrete = 0x10000;
// An *AnimationUsingKeyFrames: the frames follow as kAnimationKeyFrame(Resource) instructions.
constexpr u32 kXamlKeyFrames = 0x20000;
// Key frame interpolation in c: 0 discrete, 1 linear, else an easing packed as for animations.
constexpr u32 kXamlKeyFrameDiscrete = 0;
constexpr u32 kXamlKeyFrameLinear = 1;
constexpr u32 kXamlThemeKeyFrame = 0x80000000u;

enum class XamlRelativeSource : u32
{
    kSelf = 0,
    kTemplatedParent = 1,
};

enum class XamlThemeDictionary : u32
{
    kLight = 0,
    kDark = 1,
    kDefault = 2, // used for a theme without its own dictionary
    kHighContrast = 3,
};

struct XamlIRInstruction
{
    XamlIROp op = XamlIROp::kEndObject;
    u32 a = 0, b = 0, c = 0;
    u32 line = 0, column = 0; // source position, for diagnostics (SourceMap)
    friend bool operator==(const XamlIRInstruction&, const XamlIRInstruction&) = default;
};

struct XamlIR
{
    // Hash of the element schema the file was compiled against; a reader built for another
    // schema refuses it.
    u64 schemaFingerprint = 0;
    std::string sourceName; // file name the IR was compiled from
    std::string className;  // x:Class of the root ("Demo.MainPage"), empty when none
    std::vector<std::string> strings;
    std::vector<XamlIRType> types;
    std::vector<XamlIRProperty> properties;
    std::vector<PropertyValue> values; // literals: bool, integer, double, string, thickness, grid lengths
    std::vector<XamlIRInstruction> instructions;

    u32 intern(std::string_view text);
    u32 type(std::string_view className);
    u32 property(u64 id, std::string_view name);
    u32 value(PropertyValue value);
};

// Checks the structure: balanced objects and blocks, exactly one root (an object, or a
// ResourceDictionary file's single resources block), every index in range, and every instruction in a
// context that allows it (setters only in styles, resource entries only in resources, one object per
// template, TemplateBinding only inside a template). The message of
// the first problem, or empty when the IR is well formed. Readers run this before anything uses
// the IR, so a loader never indexes out of range.
[[nodiscard]] std::string validate(const XamlIR& ir);

// Human-readable listing (velorac dump-xbf).
[[nodiscard]] std::string dump(const XamlIR& ir);

} // namespace velora
