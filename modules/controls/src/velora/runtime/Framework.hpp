#pragma once
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/LayoutTypes.hpp>
#include <velora/controls/SourceControlRegistry.hpp>
#include <velora/globals.hpp>
#include <velora/runtime/BindingEngine.hpp>
#include <velora/runtime/ElementTree.hpp>
#include <velora/runtime/FrameworkResources.hpp>
#include <velora/runtime/FrameworkSchema.hpp>
#include <velora/runtime/VisualBridge.hpp>

// The built-in UI framework behind generated XAML code: element types and properties from
// framework_schema.hpp, layout (measure/arrange), painting through the composition bridge,
// hit testing, pointer/text/IME input, scrolling of items hosts and {Binding}. Everything
// runs on the UI thread; separate Framework instances (one per window) share no state.
//
// Compositor is only referenced, so it is forward-declared via VisualBridge.hpp; code that
// constructs one includes <velora/composition/Compositor.hpp> itself.
namespace velora
{

struct PopupOptions
{
    bool lightDismiss = true;
    bool centered = false;
    bool nonModal = false;
    bool takeFocus = true;
    // Called for Escape or outside-pointer dismissal. False keeps the current popup open.
    std::function<bool()> dismissRequested;
};

// Width of a single line of text; the default approximates it without a font backend.
using TextMeasurer = std::function<float(std::string_view text, float fontSize)>;
float approximateTextWidth(std::string_view text, float fontSize);
inline float lineHeight(float fontSize)
{
    return fontSize * 1.33f;
}

enum class Theme
{
    kLight,
    kDark,
    kHighContrast
};

// A value that depends on the theme (ThemeResource): [Light, Dark, HighContrast]. Without a high
// contrast value the dark one is used, as WinUI falls back to its Default (dark) dictionary.
struct Themed
{
    PropertyValue light, dark, highContrast;
    const PropertyValue& operator[](Theme t) const noexcept
    {
        if (t == Theme::kHighContrast)
            return highContrast.empty() ? dark : highContrast;
        return t == Theme::kLight ? light : dark;
    }
};

// Easing of an animation segment (EasingFunction + EasingMode).
struct Easing
{
    enum class Kind : u8
    {
        kLinear,
        kQuadratic,
        kCubic,
        kQuartic,
        kQuintic,
        kSine,
        kExponential,
        kCircle
    } kind = Kind::kLinear;
    enum class Mode : u8
    {
        kIn,
        kOut,
        kInOut
    } mode = Mode::kOut;
    double apply(double t) const;
};

// Visual state data, compiled from VisualStateManager.VisualStateGroups by velorac.
struct StateSetter
{
    ElementId target = 0;
    const PropertyDescriptor* property = nullptr;
    Themed value;
};
struct KeyFrame
{
    double time = 0; // seconds from the storyboard start
    Themed value;
    enum class Interpolation : u8
    {
        kDiscrete,
        kLinear,
        kEased
    } interpolation = Interpolation::kLinear;
    Easing easing;
};
// One animated property: keyframes in time order. Without `from`, the first segment starts at
// the value the property had when the state was entered. Durations are finite by construction.
struct AnimationTrack
{
    ElementId target = 0;
    const PropertyDescriptor* property = nullptr;
    std::optional<Themed> from;
    std::vector<KeyFrame> frames;
    double begin = 0; // BeginTime: the property keeps its base value until then
};
struct VisualState
{
    std::string name;
    std::vector<StateSetter> setters;
    std::vector<AnimationTrack> storyboard;
};
// A generated VisualTransition: when the group changes from `from` to `to` (empty: any), the values
// the change sets or releases move from their current value over `duration` seconds instead of
// jumping. The most specific match wins (from and to, then to, then from, then the default).
struct VisualTransition
{
    std::string from, to;
    double duration = 0;
    std::optional<Easing> easing;
};
struct VisualStateGroup
{
    std::string name;
    std::vector<VisualState> states;
    std::vector<VisualTransition> transitions;
};

class Framework
{
  public:
    struct Properties
    {
#define VELORA_FRAMEWORK_PROPERTY(member, name, kind, choices, value, flags)                                 \
    const PropertyDescriptor* member = nullptr;
        VELORA_UI_PROPERTIES(VELORA_FRAMEWORK_PROPERTY)
#undef VELORA_FRAMEWORK_PROPERTY
        // Runtime state, not settable from XAML.
        const PropertyDescriptor* IsPressed = nullptr;
        const PropertyDescriptor* IsFocused = nullptr;
        const PropertyDescriptor* IsPointerOver = nullptr;
        const PropertyDescriptor* HasTemplate = nullptr; // a ControlTemplate replaces the built-in look
        // TextBox editing: byte offsets into Text, and the IME composition shown at the caret.
        const PropertyDescriptor* CaretPosition = nullptr;
        const PropertyDescriptor* SelectionAnchor = nullptr;
        const PropertyDescriptor* PreeditText = nullptr;
        const PropertyDescriptor* PreeditCursor = nullptr; // code points into PreeditText
        // ListView: scroll position, item count and each realized container's item index.
        const PropertyDescriptor* ScrollOffset = nullptr;
        const PropertyDescriptor* ItemCount = nullptr;
        const PropertyDescriptor* ItemIndex = nullptr;
        // ScrollViewer: offsets (changeView), and the content extent and viewport from layout.
        const PropertyDescriptor* HorizontalOffset = nullptr;
        const PropertyDescriptor* VerticalOffset = nullptr;
        const PropertyDescriptor* ExtentWidth = nullptr;
        const PropertyDescriptor* ExtentHeight = nullptr;
        const PropertyDescriptor* ViewportWidth = nullptr;
        const PropertyDescriptor* ViewportHeight = nullptr;
        // ScrollViewer: bit 1 vertical, bit 2 horizontal scroll bar expanded (pointer over it
        // or dragging its thumb); collapsed bars draw as thin panning indicators.
        const PropertyDescriptor* ScrollBarExpanded = nullptr;
    };
    struct Events
    {
#define VELORA_FRAMEWORK_EVENT(member, name, signature) const EventDescriptor* member = nullptr;
        VELORA_UI_EVENTS(VELORA_FRAMEWORK_EVENT)
#undef VELORA_FRAMEWORK_EVENT
        // UIElement routed input events (R05.4). They bubble from the element under the pointer
        // (or with focus) to the root; interactive controls mark pointer events Handled in their
        // class handling, as UWP ButtonBase/TextBox/Slider do, so ancestors see them only with
        // handledEventsToo.
        const EventDescriptor* PointerPressed = nullptr;
        const EventDescriptor* PointerReleased = nullptr;
        const EventDescriptor* PointerMoved = nullptr;
        const EventDescriptor* KeyDown = nullptr;
        const EventDescriptor* GotFocus = nullptr;
        const EventDescriptor* LostFocus = nullptr;
    };

    explicit Framework(Compositor& compositor, TextMeasurer measure = approximateTextWidth);
    // Defines the schema and runtime-state properties into `registry` (sharing the visual ones).
    // One code path for every Framework and for the process-wide element properties, so all of
    // them agree on ids, defaults and flags (velora/controls/ElementFactory.hpp).
    static void defineProperties(PropertyRegistry& registry, const VisualProperties& visual,
                                 Properties& props);
    ~Framework();
    Framework(const Framework&) = delete;
    Framework& operator=(const Framework&) = delete;

    ElementTree& tree() noexcept
    {
        return m_tree;
    }
    const ElementTree& tree() const noexcept
    {
        return m_tree;
    }
    const Properties& props() const noexcept
    {
        return m_props;
    }
    const Events& events() const noexcept
    {
        return m_events;
    }
    // Descriptor for a schema (XAML) property name, e.g. "Grid.Row".
    const PropertyDescriptor* property(std::string_view xamlName) const;
    VisualBridge& bridge() noexcept
    {
        return *m_bridge;
    }

    // Non-owning content references expire before this framework is destroyed.
    std::weak_ptr<void> lifetime() const noexcept
    {
        return m_lifetime;
    }

    bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == m_ownerThread;
    }

    // Creates an element of a built-in type with the type's defaults applied at DefaultStyle level.
    // Code that only has a type name resolves it with findElementKind first.
    ElementId create(ElementKind kind, ElementId parent = 0);
    // Source-only registration. Declarations are copied; descriptors must outlive this UI context.
    std::string registerSourceControl(const XamlTypeDeclaration& declaration,
                                      SourceControlRegistry::Factory factory,
                                      std::span<const PropertyDescriptor* const> properties);
    [[nodiscard]] const SourceControlRegistry& sourceControls() const noexcept
    {
        return m_sourceControls;
    }
    // Hosts an element object made off-tree (createElement, configured through elementProperties())
    // under `parent` (0: a top-level element). Its values are kept. 0 for an untyped element, one
    // already in a tree, an unknown parent or a call off the UI thread.
    ElementId insert(std::shared_ptr<UIElement> element, ElementId parent = 0);
    void set(ElementId element, const PropertyDescriptor* property, PropertyValue value,
             PropertySource source = PropertySource::kLocal);
    void clear(ElementId element, const PropertyDescriptor* property,
               PropertySource source = PropertySource::kLocal);
    ResourceScopeStore& resources() noexcept
    {
        return *m_resources;
    }
    ElementId createDetached(ElementKind kind, ElementId pageAnchor);
    bool insertChild(ElementId parent, ElementId child, usize index);
    bool setContentObject(ElementId owner, ElementId child, PropertySource source = PropertySource::kLocal);
    ElementId contentObject(ElementId owner) const;
    ElementId pageOwner(ElementId element) const;
    // Mounting of a top-level element (R04.4, used by XamlRoot as its Content changes). An
    // unmounted element keeps all its state but is neither laid out, drawn nor hit-tested, and
    // loses focus, pointer capture, hover and drag state inside it. False for an element that
    // has a logical parent or does not exist.
    bool setMounted(ElementId element, bool mounted);
    [[nodiscard]] bool isMounted(ElementId element) const;
    // True when `element` is `ancestor` or below it in the logical tree.
    [[nodiscard]] bool isWithin(ElementId element, ElementId ancestor) const;
    // One popup per window, with modal, non-modal or passive input behavior. Success transfers the root's
    // lifetime to this service; close removes it. Content remains in the same tree/compositor.
    bool openPopup(ElementId anchor, ElementId root, std::function<void()> closed = {},
                   ElementId passThrough = 0, bool passive = false, PopupOptions options = {});
    void closePopup(bool restoreFocus = true);
    bool setPopupLightDismiss(ElementId root, bool enabled);
    [[nodiscard]] ElementId popupRoot() const noexcept;

    // ThemeResource values: applied for the current theme now and again on every theme change.
    void setThemed(ElementId element, const PropertyDescriptor* property, Themed value,
                   PropertySource source = PropertySource::kLocal);
    // StaticResource that resolved through a theme dictionary: the current theme's value, once.
    void setForTheme(ElementId element, const PropertyDescriptor* property, const Themed& value,
                     PropertySource source = PropertySource::kLocal)
    {
        set(element, property, value[m_theme], source);
    }
    // Also sets the compositor's clear colour to the theme's window background.
    void setTheme(Theme theme);
    Theme theme() const noexcept
    {
        return m_theme;
    }

    // ControlTemplate support. applyTemplate marks `owner` as templated (its built-in look and
    // background give way to the template root) and registers the instance's name scope.
    void applyTemplate(ElementId owner, ElementId root, std::vector<std::pair<std::string, ElementId>> names);
    void removeTemplate(ElementId owner);
    // OnApplyTemplate (R09.5): called after a template instance is in place, with its name scope
    // registered (templateChild works). Tokens never repeat; 0 is never a token.
    u64 addTemplateApplied(std::function<void(ElementId owner)> handler);
    bool removeTemplateApplied(u64 token);
    // {TemplateBinding}: keeps target.property equal to owner.ownerProperty (Template source).
    void templateBind(ElementId target, const PropertyDescriptor* property, ElementId owner,
                      const PropertyDescriptor* ownerProperty);
    // GetTemplateChild: a named element of the owner's template instance, or 0. A DataTemplate
    // instance (an items container) is its own owner: templateChild(containerRoot, "Label").
    ElementId templateChild(ElementId owner, std::string_view name) const;
    // Registers the name scope of a template instance under `owner` (applyTemplate does this for
    // ControlTemplates). It goes with the owner.
    void setNameScope(ElementId owner, std::vector<std::pair<std::string, ElementId>> names);

    // VisualStateManager. Templated Buttons enter CommonStates (Normal, PointerOver, Pressed,
    // Disabled) automatically; goToState selects any state explicitly. Setters and animated
    // values use the Animation source, so they override local values while the state is active.
    void setVisualStates(ElementId owner, std::vector<VisualStateGroup> groups);
    bool goToState(ElementId owner, std::string_view state);
    std::string_view currentState(ElementId owner, std::string_view group) const;
    // Animation clock in seconds; call before update() each frame. animating() asks for frames.
    void advance(double seconds);
    bool animating() const noexcept;
    // The handler is owned by the element and destroyed with it.
    void on(ElementId element, const EventDescriptor* event, std::function<void()> handler);
    // UIElement.AddHandler/RemoveHandler for routed events; returns a token (0 on failure).
    u64 addHandler(ElementId element, const EventDescriptor* event, ElementTree::RoutedHandler handler,
                   bool handledEventsToo = false);
    bool removeHandler(ElementId element, u64 token);
    // Independent registration, automatically removed by the returned UI-thread scope owner.
    // Does not replace on() or other subscriptions. Empty on invalid target/event/handler/thread.
    [[nodiscard]] EventSubscription subscribeEvent(ElementId element, const EventDescriptor* event,
                                                   ElementTree::RoutedHandler handler,
                                                   bool handledEventsToo = false);

    // {Binding}: `path` is relative to the element's DataContext (see BindingEngine).
    void bind(ElementId element, const PropertyDescriptor* property, std::string_view path,
              BindingMode mode = BindingMode::kOneWay, BindingOptions options = {});
    // {Binding ElementName/RelativeSource}: `property` follows `source`'s `sourceProperty` (and, TwoWay,
    // writes back). The binding ends when either element is removed.
    void bindElement(ElementId element, const PropertyDescriptor* property, ElementId source,
                     const PropertyDescriptor* sourceProperty, BindingMode mode, BindingOptions options = {});
    [[nodiscard]] usize elementBindingCount() const noexcept
    {
        return m_elementBindings.size();
    }
    // {x:Bind}: `property` shows `get()` and, unless OneTime, follows the path's change notifications;
    // TwoWay writes the target's changes through `set`. Ends with the element.
    void bindAccessor(ElementId element, const PropertyDescriptor* property,
                      std::function<std::optional<PropertyValue>()> get,
                      std::function<std::vector<Subscription>(std::function<void()>)> observe,
                      std::function<bool(const PropertyValue&)> set, BindingMode mode,
                      bool contextual = false);
    [[nodiscard]] usize accessorBindingCount() const noexcept
    {
        return m_accessorBindings.size();
    }
    BindingStats bindingStats() const
    {
        return m_bindings->stats();
    }
    const std::vector<std::string>& bindingErrors() const noexcept
    {
        return m_bindings->errors();
    }

    // Lays out every root in a width x height viewport when anything changed, then syncs visuals.
    void update(float width, float height);
    Box bounds(ElementId element) const; // absolute arranged rectangle
    ElementId hitTest(float x, float y) const;

    void pointerMove(float x, float y);
    void pointerDown(float x, float y);
    void pointerUp(float x, float y);
    // Text input into the focused TextBox. textInput commits text (replacing the selection and
    // ending any composition); textEditing shows IME preedit text at the caret without
    // changing Text, so TwoWay sources only see committed text. Keys are ignored while a
    // composition is active (the IME owns them). Moving focus away commits the composition.
    // TextChanged is raised for user edits only, never when Text is set programmatically.
    void textInput(std::string_view utf8);
    void textEditing(std::string_view utf8, std::size_t cursor);
    void key(Key key, bool shift = false, bool ctrl = false, bool alt = false);
    // Tab navigation (R10.5): the next (or previous) tab stop — an enabled, visible TextBox, Button,
    // CheckBox, RadioButton, ToggleSwitch or Slider in mounted content with IsTabStop — ordered by
    // TabIndex, then tree order, wrapping at the ends. False when there is none.
    bool moveFocus(bool forward);
    // UIElement.Focus: moves focus to `element` (0 clears it), committing a TextBox composition.
    void focus(ElementId element);
    ElementId focused() const noexcept
    {
        return m_tree.focused();
    }
    // The focused TextBox caret in window coordinates (for the IME candidate window); empty
    // when no TextBox has focus.
    Box caretBounds() const;

    // One ScrollViewer scroll bar in the viewer's local coordinates. `shown` is false when the
    // axis has no bar (Disabled, Hidden, or Auto with nothing to scroll); the other boxes are
    // then empty. Arrows and track are hit targets only while the bar is expanded.
    struct ScrollBarParts
    {
        bool shown = false, expanded = false;
        Box bar, decrease, increase, track, thumb;
    };
    ScrollBarParts scrollBar(ElementId viewer, bool vertical) const;
    // Scrolls the innermost ListView or ScrollViewer under (x, y) that can still move in that
    // direction; positive delta moves the content up (a ScrollViewer that cannot scroll
    // vertically scrolls horizontally).
    void wheel(float x, float y, float delta);
    // ScrollViewer.ChangeView: moves to the given offsets, clamped to the scrollable range of the
    // last layout (before the first layout, by the next one); an absent offset is kept. False
    // when `scroller` is not a ScrollViewer.
    bool changeView(ElementId scroller, std::optional<double> horizontal, std::optional<double> vertical);
    void scrollTo(ElementId list, double offset);
    struct Viewport
    {
        double offset = 0, height = 0, itemHeight = 0;
        std::size_t count = 0;
        friend bool operator==(const Viewport&, const Viewport&) = default;
    };
    Viewport viewport(ElementId list) const;
    // Called after layout whenever the ListView's viewport changes; owned by the element.
    void onViewport(ElementId list, std::function<void()> callback);

  private:
    Size measure(ElementId element, Size available);
    void arrange(ElementId element, Box slot);
    // The element's own MeasureOverride/ArrangeOverride (velora/controls), with the common inputs.
    Size measureContent(ElementId element, Size available);
    void arrangeContent(ElementId element, Box content);
    friend class LayoutContext;
    ElementId hit(ElementId element, float x, float y, float originX, float originY) const;
    // Framework-side state of a new element of `kind` (scroll offsets, list viewports, page domain).
    void initializeElement(ElementId id, ElementKind kind);
    // Raises a routed input event with the class handling of the elements on the route.
    bool raiseRouted(ElementId target, const EventDescriptor* event, RoutedEventArgs* input = nullptr);
    void registerPainters();
    void applyWindowBackground();
    // ScrollViewer input (framework_scroll.cpp).
    bool scrollStep(ElementId viewer, bool vertical, double delta);
    bool scrollKey(Key key);
    bool scrollBarDown(float x, float y);
    void scrollBarMove(float x, float y);
    bool notifyViewChanged();
    // Toggle and range controls (framework_controls.cpp).
    static bool pressable(std::optional<ElementKind> kind);
    static bool interactive(std::optional<ElementKind> kind);
    void toggle(ElementId element);
    void controlValueChanged(ElementId element, const PropertyDescriptor* property);
    void sliderTo(ElementId slider, float x);
    bool controlKey(Key key);
    void layout(float width, float height);
    bool notifyViewports();
    // TextBox editing helpers.
    struct Edit
    {
        std::string text;
        std::size_t caret = 0, anchor = 0;
    };
    Edit editState(ElementId box) const;
    void applyEdit(ElementId box, const Edit& edit, bool textChanged);
    std::size_t caretAt(ElementId box, float x) const;
    // Visual states.
    struct StateOwner;
    struct RunningTrack;
    void enterState(ElementId owner, StateOwner& states, std::size_t group, std::size_t state);
    void updateCommonState(ElementId element);
    void evaluate(RunningTrack& run, double now);

    double number(ElementId e, const PropertyDescriptor* d) const;
    // ScrollViewer axis whose ScrollBarVisibility (`d`) is not Disabled.
    bool scrollable(ElementId e, const PropertyDescriptor* d) const;
    Thickness thickness(ElementId e, const PropertyDescriptor* d) const;
    std::string_view text(ElementId e, const PropertyDescriptor* d) const;

    std::unique_ptr<ResourceScopeStore> m_resources;
    std::unordered_map<ElementId, ElementId> m_detachedPages;
    std::unordered_map<ElementId, ElementId> m_templateRoots;
    std::unordered_map<ElementId, ElementId> m_contentPresenters;
    void contentChanged(ElementId owner);
    void setContentValue(ElementId owner, PropertyValue value, PropertySource source);
    friend class EventSubscription;
    std::shared_ptr<bool> m_lifetime = std::make_shared<bool>(true);
    std::thread::id m_ownerThread = std::this_thread::get_id();
    Compositor& m_compositor;
    TextMeasurer m_measure;
    PropertyRegistry m_registry;
    SourceControlRegistry m_sourceControls;
    EventRegistry m_eventRegistry;
    VisualProperties m_visual;
    Properties m_props;
    Events m_events;
    struct ListState
    {
        Viewport current, notified;
        std::function<void()> callback;
    };
    std::unordered_map<ElementId, ListState> m_lists;
    Theme m_theme = Theme::kLight;
    struct ThemedValue
    {
        const PropertyDescriptor* property;
        PropertySource source;
        Themed value;
    };
    std::unordered_map<ElementId, std::vector<ThemedValue>> m_themed;
    struct TemplateBinding
    {
        ElementId target;
        const PropertyDescriptor* property;
        const PropertyDescriptor* ownerProperty;
    };
    std::unordered_map<ElementId, std::vector<TemplateBinding>> m_templateBindings; // by owner
    std::unordered_map<ElementId, std::vector<std::pair<std::string, ElementId>>> m_nameScopes;
    struct StateOwner
    {
        std::vector<VisualStateGroup> groups;
        std::vector<std::size_t> current;    // per group; npos = none
        std::list<AnimationTrack> generated; // tracks of running VisualTransitions (stable addresses)
    };
    std::unordered_map<ElementId, StateOwner> m_states;
    struct RunningTrack
    {
        ElementId owner;
        const AnimationTrack* track;
        PropertyValue start; // value when the state was entered
        double begin;
        bool done = false;
        bool release = false; // a transition back to the base value: drop the animated value when done
    };
    std::vector<RunningTrack> m_running;
    double m_clock = 0;
    ElementId m_hovered = 0;
    std::unique_ptr<BindingEngine> m_bindings;
    std::vector<std::pair<u64, std::shared_ptr<std::function<void(ElementId)>>>> m_templateApplied;
    u64 m_nextTemplateToken = 1;
    struct ElementBinding
    {
        ElementId target = 0;
        const PropertyDescriptor* property = nullptr;
        ElementId source = 0;
        const PropertyDescriptor* sourceProperty = nullptr;
        BindingMode mode = BindingMode::kOneWay;
        BindingOptions options;
        bool writing = false;
    };
    void elementPropertyChanged(ElementId element, const PropertyDescriptor& property);
    void pushElementBinding(ElementBinding& binding);
    std::vector<std::shared_ptr<ElementBinding>> m_elementBindings;
    struct AccessorBinding
    {
        ElementId target = 0;
        const PropertyDescriptor* property = nullptr;
        std::function<std::optional<PropertyValue>()> get;
        std::function<std::vector<Subscription>(std::function<void()>)> observe;
        std::function<bool(const PropertyValue&)> set;
        BindingMode mode = BindingMode::kOneTime;
        std::vector<Subscription> subscriptions;
        bool writing = false;
        bool contextual = false; // reads the element's DataContext: evaluated again when it changes
    };
    void refreshContextual(ElementId element);
    void pushAccessorBinding(const std::shared_ptr<AccessorBinding>& binding);
    std::vector<std::shared_ptr<AccessorBinding>> m_accessorBindings;
    // Declared after everything element cleanups touch, so the tree is destroyed first.
    ElementTree m_tree;
    std::unique_ptr<VisualBridge> m_bridge;
    std::unordered_map<ElementId, Size> m_desired;
    float m_width = -1, m_height = -1;
    ElementId m_pressed = 0;
    struct PopupState
    {
        ElementId anchor = 0, root = 0, restore = 0;
        std::function<void()> closed;
        ElementId passThrough = 0;
        bool passive = false; // informational overlay: no focus, input interception or Tab scope
        PopupOptions options;
    };
    std::optional<PopupState> m_popup;
    bool m_dismissedPopupPress = false;
    bool m_destroying = false;
    bool popupAnchorAvailable(ElementId element) const;
    void layoutPopup();
    // ScrollViewer state: the thumb being dragged, the viewer whose bar is expanded, and the
    // offsets last reported through ViewChanged.
    struct ScrollDrag
    {
        ElementId viewer = 0;
        bool vertical = true;
        float grab = 0; // pointer position along the thumb when it was pressed
    } m_scrollDrag;
    ElementId m_scrollHover = 0;
    ElementId m_sliderDrag = 0; // Slider whose thumb follows the pointer
    std::unordered_map<ElementId, std::pair<double, double>> m_viewOffsets;
};

} // namespace velora
