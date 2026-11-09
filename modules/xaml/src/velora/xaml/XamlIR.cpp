#include <algorithm>
#include <format>
#include <velora/xaml/XamlIR.hpp>

namespace velora
{

u32 XamlIR::intern(std::string_view text)
{
    for (u32 i = 0; i < strings.size(); ++i)
        if (strings[i] == text)
            return i;
    strings.emplace_back(text);
    return static_cast<u32>(strings.size() - 1);
}

u32 XamlIR::type(std::string_view className)
{
    for (u32 i = 0; i < types.size(); ++i)
        if (types[i].className == className)
            return i;
    types.push_back({typeIdFor(className), std::string(className)});
    return static_cast<u32>(types.size() - 1);
}

u32 XamlIR::property(u64 id, std::string_view name)
{
    for (u32 i = 0; i < properties.size(); ++i)
        if (properties[i].id == id && properties[i].name == name)
            return i;
    properties.push_back({id, std::string(name)});
    return static_cast<u32>(properties.size() - 1);
}

u32 XamlIR::value(PropertyValue literal)
{
    for (u32 i = 0; i < values.size(); ++i)
        if (values[i] == literal)
            return i;
    values.push_back(std::move(literal));
    return static_cast<u32>(values.size() - 1);
}

std::string validate(const XamlIR& ir)
{
    // What the instructions are nested in; each block kind allows a fixed set of instructions.
    enum class Block : u8
    {
        kObject,
        kResources,
        kThemeDictionary,
        kStyle,
        kTemplate,
        kDataTemplate,
        kVisualStates,
        kStateGroup,
        kState,
        kAnimation,
    };
    struct Open
    {
        Block block;
        u32 objects = 0; // for kTemplate: its objects (exactly one when it closes)
    };
    std::vector<Open> stack;
    usize roots = 0;
    bool bindingOptions = false; // options read, waiting for their kSetBinding
    const auto in = [&](std::initializer_list<Block> blocks)
    { return !stack.empty() && std::ranges::find(blocks, stack.back().block) != blocks.end(); };
    const auto insideTemplate = [&]
    { return std::ranges::any_of(stack, [](const Open& o) { return o.block == Block::kTemplate; }); };
    for (usize i = 0; i < ir.instructions.size(); ++i)
    {
        const auto& op = ir.instructions[i];
        const auto where = [&](std::string_view what) { return std::format("instruction {}: {}", i, what); };
        const auto stringOk = [&](u32 index) { return index < ir.strings.size(); };
        const auto keyOk = [&](u32 index) { return index == kXamlNoIndex || stringOk(index); };
        const auto propertyOk = [&](u32 index) { return index < ir.properties.size(); };
        const auto push = [&](Block block)
        {
            stack.push_back({block});
            return stack.size() <= kXamlMaxNesting;
        };
        bool nested = true;
        const bool option = op.op == XamlIROp::kBindElementName || op.op == XamlIROp::kBindRelativeSource ||
                            op.op == XamlIROp::kBindConverter || op.op == XamlIROp::kBindFallback;
        if (bindingOptions && !option && op.op != XamlIROp::kSetBinding)
            return where("binding options without their binding");
        bindingOptions = option;
        switch (op.op)
        {
        case XamlIROp::kBeginObject:
            if (op.a >= ir.types.size())
                return where("type index out of range");
            if (stack.empty())
            {
                if (++roots > 1)
                    return where("more than one root object");
            }
            else if (in({Block::kTemplate, Block::kDataTemplate}))
            {
                if (++stack.back().objects > 1)
                    return where("a template has exactly one root object");
            }
            else if (!in({Block::kObject}))
                return where("an object is not allowed here");
            nested = push(Block::kObject);
            break;
        case XamlIROp::kEndObject:
            if (!in({Block::kObject}))
                return where("EndObject without an object");
            stack.pop_back();
            break;
        case XamlIROp::kSetValue:
            if (!in({Block::kObject}) || !propertyOk(op.a) || op.b >= ir.values.size())
                return where("SetValue outside an object or index out of range");
            break;
        case XamlIROp::kSetBinding:
            if (!in({Block::kObject}) || !propertyOk(op.a) || !stringOk(op.b) || op.c > 2)
                return where("SetBinding outside an object or index out of range");
            break;
        case XamlIROp::kSetResource:
            if (!in({Block::kObject}) || !propertyOk(op.a) || !stringOk(op.b) || op.c > 1)
                return where("SetResource outside an object or index out of range");
            break;
        case XamlIROp::kSetName:
            if (!in({Block::kObject}) || !stringOk(op.b))
                return where("SetName outside an object or index out of range");
            break;
        case XamlIROp::kAddEvent:
            if (!in({Block::kObject}) || !stringOk(op.b) || !stringOk(op.c))
                return where("AddEvent outside an object or index out of range");
            break;
        case XamlIROp::kResourceOpacity:
            if (!in({Block::kResources, Block::kThemeDictionary}) || !stringOk(op.a) ||
                op.b >= ir.values.size())
                return where("a brush opacity outside a dictionary or index out of range");
            break;
        case XamlIROp::kBeginVisualStates:
            if (!in({Block::kObject}))
                return where("visual states outside an object");
            nested = push(Block::kVisualStates);
            break;
        case XamlIROp::kEndVisualStates:
            if (!in({Block::kVisualStates}))
                return where("EndVisualStates without visual states");
            stack.pop_back();
            break;
        case XamlIROp::kBeginStateGroup:
            if (!in({Block::kVisualStates}) || !stringOk(op.b))
                return where("a state group outside visual states");
            nested = push(Block::kStateGroup);
            break;
        case XamlIROp::kEndStateGroup:
            if (!in({Block::kStateGroup}))
                return where("EndStateGroup without a group");
            stack.pop_back();
            break;
        case XamlIROp::kBeginState:
            if (!in({Block::kStateGroup}) || !stringOk(op.b))
                return where("a visual state outside a group");
            nested = push(Block::kState);
            break;
        case XamlIROp::kEndState:
            if (!in({Block::kState}))
                return where("EndState without a state");
            stack.pop_back();
            break;
        case XamlIROp::kStateSetter:
            if (!in({Block::kState}) || !propertyOk(op.a) || op.b >= ir.values.size() || !stringOk(op.c))
                return where("a state setter outside a state or index out of range");
            break;
        case XamlIROp::kStateSetterResource:
        case XamlIROp::kStateSetterTheme:
            if (!in({Block::kState}) || !propertyOk(op.a) || !stringOk(op.b) || !stringOk(op.c))
                return where("a state setter outside a state or index out of range");
            break;
        case XamlIROp::kBeginAnimation:
            if (!in({Block::kState}) || !propertyOk(op.a) || !stringOk(op.b))
                return where("an animation outside a state or index out of range");
            nested = push(Block::kAnimation);
            break;
        case XamlIROp::kAnimationValue:
            if (!in({Block::kAnimation}) || op.a > 1 || op.b >= ir.values.size())
                return where("an animation value outside an animation or index out of range");
            break;
        case XamlIROp::kAnimationResource:
            if (!in({Block::kAnimation}) || op.a > 3 || !stringOk(op.b))
                return where("an animation value outside an animation or index out of range");
            break;
        case XamlIROp::kSetItemXBind:
            if (!in({Block::kObject}) || !propertyOk(op.a) || op.c > 2 ||
                std::ranges::none_of(stack, [](const Open& o) { return o.block == Block::kDataTemplate; }))
                return where("an item x:Bind outside a DataTemplate");
            break;
        case XamlIROp::kStyleTemplateReference:
            if (!in({Block::kStyle}) || !stringOk(op.b))
                return where("a Template setter outside a style");
            break;
        case XamlIROp::kBeginDataTemplate:
            if (op.a != kXamlNoIndex ? !in({Block::kResources, Block::kThemeDictionary}) || !stringOk(op.a)
                                     : !in({Block::kObject}))
                return where("a DataTemplate is not allowed here");
            nested = push(Block::kDataTemplate);
            break;
        case XamlIROp::kEndDataTemplate:
            if (!in({Block::kDataTemplate}) || stack.back().objects != 1)
                return where("a DataTemplate has exactly one root object");
            stack.pop_back();
            break;
        case XamlIROp::kSetItemTemplate:
            if (!in({Block::kObject}) || !stringOk(op.b))
                return where("ItemTemplate outside an object");
            break;
        case XamlIROp::kSetItemsSource:
            if (!in({Block::kObject}) || op.c > 2)
                return where("ItemsSource outside an object");
            break;
        case XamlIROp::kStateTransition:
            if (!in({Block::kStateGroup}) || !keyOk(op.a) || !keyOk(op.b))
                return where("a transition outside a state group or index out of range");
            break;
        case XamlIROp::kTransitionEasing:
            if (!in({Block::kStateGroup}) || i == 0 ||
                ir.instructions[i - 1].op != XamlIROp::kStateTransition)
                return where("a transition easing without its transition");
            break;
        case XamlIROp::kAnimationKeyFrame:
            if (!in({Block::kAnimation}) || op.b >= ir.values.size())
                return where("a key frame outside an animation or index out of range");
            break;
        case XamlIROp::kAnimationKeyFrameResource:
            if (!in({Block::kAnimation}) || !stringOk(op.b))
                return where("a key frame outside an animation or index out of range");
            break;
        case XamlIROp::kAnimationTiming:
            if (!in({Block::kAnimation}))
                return where("animation timing outside an animation");
            break;
        case XamlIROp::kEndAnimation:
            if (!in({Block::kAnimation}))
                return where("EndAnimation without an animation");
            stack.pop_back();
            break;
        case XamlIROp::kSetXBind:
            if (!in({Block::kObject}) || insideTemplate() || !propertyOk(op.a) || op.c > 2)
                return where("x:Bind outside a page object or index out of range");
            break;
        case XamlIROp::kSetUid:
            if (!in({Block::kObject}) || !stringOk(op.b))
                return where("x:Uid outside an object or index out of range");
            break;
        case XamlIROp::kBindElementName:
            if (!in({Block::kObject}) || !stringOk(op.b))
                return where("ElementName outside an object or index out of range");
            break;
        case XamlIROp::kBindRelativeSource:
            if (!in({Block::kObject}) || op.c > static_cast<u32>(XamlRelativeSource::kTemplatedParent) ||
                (op.c == static_cast<u32>(XamlRelativeSource::kTemplatedParent) && !insideTemplate()))
                return where("RelativeSource outside an object, or TemplatedParent outside a template");
            break;
        case XamlIROp::kBindConverter:
            if (!in({Block::kObject}) || !stringOk(op.b) || !keyOk(op.c))
                return where("Converter outside an object or index out of range");
            break;
        case XamlIROp::kBindFallback:
            if (!in({Block::kObject}) || op.b >= ir.values.size())
                return where("FallbackValue outside an object or index out of range");
            break;
        case XamlIROp::kResourceConverter:
            if (!in({Block::kResources, Block::kThemeDictionary}) || !stringOk(op.a) || !stringOk(op.b))
                return where("a converter resource outside a dictionary or index out of range");
            break;
        case XamlIROp::kApplyStyle:
        case XamlIROp::kSetTemplate:
            if (!in({Block::kObject}) || !stringOk(op.b))
                return where("a style or template reference outside an object");
            break;
        case XamlIROp::kSetTemplateBinding:
            if (!in({Block::kObject}) || !insideTemplate() || !propertyOk(op.a) || !propertyOk(op.b))
                return where("TemplateBinding outside a template or index out of range");
            break;
        case XamlIROp::kBeginResources:
            if (stack.empty())
            {
                if (++roots > 1)
                    return where("more than one root");
            }
            else if (!in({Block::kObject}))
                return where("Resources outside an object");
            nested = push(Block::kResources);
            break;
        case XamlIROp::kEndResources:
            if (!in({Block::kResources}))
                return where("EndResources without Resources");
            stack.pop_back();
            break;
        case XamlIROp::kResource:
            if (!in({Block::kResources, Block::kThemeDictionary}) || !stringOk(op.a) ||
                op.b >= ir.values.size())
                return where("a resource outside a dictionary or index out of range");
            break;
        case XamlIROp::kResourceReference:
            if (!in({Block::kResources, Block::kThemeDictionary}) || !stringOk(op.a) || !stringOk(op.b) ||
                op.c > 1)
                return where("a resource reference outside a dictionary or index out of range");
            break;
        case XamlIROp::kMergeDictionary:
            if (!in({Block::kResources}) || !stringOk(op.b))
                return where("a merged dictionary outside Resources");
            break;
        case XamlIROp::kBeginThemeDictionary:
            if (!in({Block::kResources}) || op.c > static_cast<u32>(XamlThemeDictionary::kHighContrast))
                return where("a theme dictionary outside Resources");
            nested = push(Block::kThemeDictionary);
            break;
        case XamlIROp::kEndThemeDictionary:
            if (!in({Block::kThemeDictionary}))
                return where("EndThemeDictionary without a theme dictionary");
            stack.pop_back();
            break;
        case XamlIROp::kBeginStyle:
            if (!in({Block::kResources, Block::kThemeDictionary}) || !keyOk(op.a) ||
                op.b >= ir.types.size() || !keyOk(op.c))
                return where("a style outside a dictionary or index out of range");
            nested = push(Block::kStyle);
            break;
        case XamlIROp::kSetter:
            if (!in({Block::kStyle}) || !propertyOk(op.a) || op.b >= ir.values.size())
                return where("a setter outside a style or index out of range");
            break;
        case XamlIROp::kSetterResource:
            if (!in({Block::kStyle}) || !propertyOk(op.a) || !stringOk(op.b) || op.c > 1)
                return where("a setter outside a style or index out of range");
            break;
        case XamlIROp::kEndStyle:
            if (!in({Block::kStyle}))
                return where("EndStyle without a style");
            stack.pop_back();
            break;
        case XamlIROp::kBeginTemplate:
            if (op.b >= ir.types.size() || !keyOk(op.a))
                return where("template index out of range");
            if (op.a != kXamlNoIndex ? !in({Block::kResources, Block::kThemeDictionary})
                                     : !in({Block::kStyle, Block::kObject}))
                return where("a template is not allowed here");
            nested = push(Block::kTemplate);
            break;
        case XamlIROp::kEndTemplate:
            if (!in({Block::kTemplate}) || stack.back().objects != 1)
                return where("a template has exactly one root object");
            stack.pop_back();
            break;
        default:
            return where("unknown opcode");
        }
        if (!nested)
            return where("nested too deeply");
    }
    if (bindingOptions)
        return "binding options without their binding";
    if (!stack.empty())
        return "unbalanced objects";
    if (roots != 1)
        return "no root object";
    return {};
}

namespace
{
std::string literal(const PropertyValue& v)
{
    if (v.isBool())
        return v.asBool() ? "true" : "false";
    if (v.isInt())
        return std::to_string(v.asInt());
    if (v.isDouble())
        return std::format("{}", v.asDouble());
    if (v.isString())
        return "\"" + v.asString() + "\"";
    if (v.isThickness())
    {
        const auto& t = v.asThickness();
        return std::format("Thickness({},{},{},{})", t.left, t.top, t.right, t.bottom);
    }
    if (v.isGridLengths())
        return std::format("GridLengths[{}]", v.asGridLengths().size());
    return "(unset)";
}
} // namespace

std::string dump(const XamlIR& ir)
{
    std::string out = std::format("vxbf {} ({}), schema {:016x}\n", ir.sourceName,
                                  ir.className.empty() ? "no x:Class" : ir.className, ir.schemaFingerprint);
    const auto key = [&](u32 index)
    { return index == kXamlNoIndex ? std::string("(implicit)") : ir.strings[index]; };
    const auto reference = [](u32 theme) { return theme ? "{ThemeResource " : "{StaticResource "; };
    usize depth = 0;
    for (const auto& in : ir.instructions)
    {
        switch (in.op)
        {
        case XamlIROp::kEndObject:
        case XamlIROp::kEndResources:
        case XamlIROp::kEndThemeDictionary:
        case XamlIROp::kEndStyle:
        case XamlIROp::kEndTemplate:
        case XamlIROp::kEndDataTemplate:
        case XamlIROp::kEndVisualStates:
        case XamlIROp::kEndStateGroup:
        case XamlIROp::kEndState:
        case XamlIROp::kEndAnimation:
            --depth;
            continue;
        default:
            break;
        }
        out.append(2 * depth, ' ');
        switch (in.op)
        {
        case XamlIROp::kBeginObject:
            out += ir.types[in.a].className;
            ++depth;
            break;
        case XamlIROp::kSetValue:
            out += ir.properties[in.a].name + " = " + literal(ir.values[in.b]);
            break;
        case XamlIROp::kSetBinding:
            out += ir.properties[in.a].name + " = {Binding " + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kSetResource:
            out += ir.properties[in.a].name + " = " + reference(in.c) + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kSetName:
            out += "x:Name = " + ir.strings[in.b];
            break;
        case XamlIROp::kAddEvent:
            out += ir.strings[in.b] + " -> " + ir.strings[in.c];
            break;
        case XamlIROp::kApplyStyle:
            out += "Style = {StaticResource " + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kSetTemplate:
            out += "Template = {StaticResource " + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kSetTemplateBinding:
            out += ir.properties[in.a].name + " = {TemplateBinding " + ir.properties[in.b].name + "}";
            break;
        case XamlIROp::kBeginResources:
            out += "Resources";
            ++depth;
            break;
        case XamlIROp::kResource:
            out += ir.strings[in.a] + " = " + literal(ir.values[in.b]);
            break;
        case XamlIROp::kResourceReference:
            out += ir.strings[in.a] + " = " + reference(in.c) + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kMergeDictionary:
            out += "MergedDictionary " + ir.strings[in.b];
            break;
        case XamlIROp::kBeginThemeDictionary:
            out += in.c == 0   ? "ThemeDictionary Light"
                   : in.c == 1 ? "ThemeDictionary Dark"
                   : in.c == 2 ? "ThemeDictionary Default"
                               : "ThemeDictionary HighContrast";
            ++depth;
            break;
        case XamlIROp::kBeginStyle:
            out += "Style " + key(in.a) + " TargetType=" + ir.types[in.b].className +
                   (in.c == kXamlNoIndex ? "" : " BasedOn=" + ir.strings[in.c]);
            ++depth;
            break;
        case XamlIROp::kSetter:
            out += "Setter " + ir.properties[in.a].name + " = " + literal(ir.values[in.b]);
            break;
        case XamlIROp::kSetterResource:
            out += "Setter " + ir.properties[in.a].name + " = " + reference(in.c) + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kResourceOpacity:
            out += ir.strings[in.a] + " Opacity = " + literal(ir.values[in.b]);
            break;
        case XamlIROp::kBeginVisualStates:
            out += "VisualStateGroups";
            ++depth;
            break;
        case XamlIROp::kBeginStateGroup:
            out += "VisualStateGroup " + ir.strings[in.b];
            ++depth;
            break;
        case XamlIROp::kBeginState:
            out += "VisualState " + ir.strings[in.b];
            ++depth;
            break;
        case XamlIROp::kStateSetter:
            out += "Setter " + ir.strings[in.c] + "." + ir.properties[in.a].name + " = " +
                   literal(ir.values[in.b]);
            break;
        case XamlIROp::kStateSetterResource:
        case XamlIROp::kStateSetterTheme:
            out += "Setter " + ir.strings[in.c] + "." + ir.properties[in.a].name + " = " +
                   reference(in.op == XamlIROp::kStateSetterTheme ? 1 : 0) + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kBeginAnimation:
            out +=
                std::format("Animation {}.{} easing {:x}", ir.strings[in.b], ir.properties[in.a].name, in.c);
            ++depth;
            break;
        case XamlIROp::kAnimationValue:
            out += std::string(in.a ? "To = " : "From = ") + literal(ir.values[in.b]);
            break;
        case XamlIROp::kAnimationResource:
            out +=
                std::string(in.a & 1 ? "To = " : "From = ") + reference(in.a >> 1) + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kSetItemXBind:
            out += std::format("{} = {{x:Bind item #{}}}", ir.properties[in.a].name, in.b);
            break;
        case XamlIROp::kStyleTemplateReference:
            out += "Setter Template = {StaticResource " + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kBeginDataTemplate:
            out += "DataTemplate " + (in.a == kXamlNoIndex ? std::string("(inline)") : ir.strings[in.a]);
            ++depth;
            break;
        case XamlIROp::kSetItemTemplate:
            out += "ItemTemplate = {StaticResource " + ir.strings[in.b] + "}";
            break;
        case XamlIROp::kSetItemsSource:
            out += std::format("ItemsSource = {{x:Bind #{}}}", in.b);
            break;
        case XamlIROp::kStateTransition:
            out +=
                std::format("VisualTransition {} -> {} {} ms", in.a == kXamlNoIndex ? "*" : ir.strings[in.a],
                            in.b == kXamlNoIndex ? "*" : ir.strings[in.b], in.c);
            break;
        case XamlIROp::kTransitionEasing:
            out += std::format("  easing {:x}", in.a);
            break;
        case XamlIROp::kAnimationKeyFrame:
            out += std::format("KeyFrame {} ms = {} ({:x})", in.a, literal(ir.values[in.b]), in.c);
            break;
        case XamlIROp::kAnimationKeyFrameResource:
            out += std::format("KeyFrame {} ms = {}{}}} ({:x})", in.a, reference(in.c >> 31),
                               ir.strings[in.b], in.c & ~kXamlThemeKeyFrame);
            break;
        case XamlIROp::kAnimationTiming:
            out += std::format("Duration {} ms, BeginTime {} ms", in.a, in.b);
            break;
        case XamlIROp::kSetXBind:
            out += std::format("{} = {{x:Bind #{}, Mode={}}}", ir.properties[in.a].name, in.b,
                               in.c == 0   ? "OneTime"
                               : in.c == 1 ? "OneWay"
                                           : "TwoWay");
            break;
        case XamlIROp::kSetUid:
            out += "x:Uid = " + ir.strings[in.b];
            break;
        case XamlIROp::kBindElementName:
            out += "  ElementName=" + ir.strings[in.b];
            break;
        case XamlIROp::kBindRelativeSource:
            out += in.c ? "  RelativeSource=TemplatedParent" : "  RelativeSource=Self";
            break;
        case XamlIROp::kBindConverter:
            out += "  Converter=" + ir.strings[in.b] +
                   (in.c == kXamlNoIndex ? std::string() : " ConverterParameter=" + ir.strings[in.c]);
            break;
        case XamlIROp::kBindFallback:
            out += "  FallbackValue=" + literal(ir.values[in.b]);
            break;
        case XamlIROp::kResourceConverter:
            out += ir.strings[in.a] + " = converter " + ir.strings[in.b];
            break;
        case XamlIROp::kBeginTemplate:
            out += "ControlTemplate " + (in.a == kXamlNoIndex ? std::string("(inline)") : ir.strings[in.a]) +
                   " TargetType=" + ir.types[in.b].className;
            ++depth;
            break;
        default:
            break;
        }
        out += std::format("  @{}:{}\n", in.line, in.column);
    }
    return out;
}

} // namespace velora
