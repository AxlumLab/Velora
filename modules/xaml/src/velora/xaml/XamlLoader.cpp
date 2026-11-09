#include <format>
#include <utility>
#include <velora/runtime/object/BuiltinTypes.hpp>
#include <velora/xaml/XamlBinary.hpp>
#include <velora/xaml/XamlLoader.hpp>

namespace velora
{

namespace
{
bool opens(XamlIROp op) noexcept
{
    switch (op)
    {
    case XamlIROp::kBeginObject:
    case XamlIROp::kBeginResources:
    case XamlIROp::kBeginThemeDictionary:
    case XamlIROp::kBeginStyle:
    case XamlIROp::kBeginTemplate:
    case XamlIROp::kBeginDataTemplate:
    case XamlIROp::kBeginVisualStates:
    case XamlIROp::kBeginStateGroup:
    case XamlIROp::kBeginState:
    case XamlIROp::kBeginAnimation:
        return true;
    default:
        return false;
    }
}

bool closes(XamlIROp op) noexcept
{
    switch (op)
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
        return true;
    default:
        return false;
    }
}
} // namespace

usize xamlBlockEnd(const XamlIR& ir, usize begin)
{
    usize depth = 0;
    for (usize i = begin; i < ir.instructions.size(); ++i)
    {
        if (opens(ir.instructions[i].op))
            ++depth;
        else if (closes(ir.instructions[i].op) && --depth == 0)
            return i;
    }
    return ir.instructions.size(); // unreachable for a valid IR
}

XamlLoadResult XamlLoader::load(std::span<const u8> vxbf, XamlLoadTarget& target,
                                const XamlLoadOptions& options)
{
    auto read = readVxbf(vxbf, target.schemaFingerprint());
    if (auto* error = std::get_if<VxbfError>(&read))
    {
        XamlLoadResult result;
        result.error = error->message;
        return result;
    }
    return load(std::make_shared<const XamlIR>(std::move(std::get<XamlIR>(read))), target, options);
}

XamlLoadResult XamlLoader::load(const std::shared_ptr<const XamlIR>& ir, XamlLoadTarget& target,
                                const XamlLoadOptions& options)
{
    XamlLoadResult result;
    if (auto message = validate(*ir); !message.empty())
    {
        result.error = ir->sourceName + ": " + message;
        return result;
    }
    if (ir->instructions.front().op != XamlIROp::kBeginObject)
    {
        result.error = ir->sourceName + ": a ResourceDictionary file is merged into resources, not loaded";
        return result;
    }
    return run(ir, 0, ir->instructions.size(), target, options, 0);
}

XamlLoadResult XamlLoader::instantiateTemplate(const std::shared_ptr<const XamlIR>& ir, usize begin,
                                               XamlLoadTarget& target, XamlHandle owner)
{
    XamlLoadOptions options;
    options.parent = owner;
    auto result = run(ir, begin + 1, xamlBlockEnd(*ir, begin), target, options, owner);
    if (result.ok)
        target.completeTemplate(owner, result.root, result.names);
    return result;
}

XamlLoadResult XamlLoader::instantiateDataTemplate(const std::shared_ptr<const XamlIR>& ir, usize begin,
                                                   XamlLoadTarget& target, const XamlLoadOptions& options)
{
    return run(ir, begin + 1, xamlBlockEnd(*ir, begin), target, options, 0);
}

XamlLoadResult XamlLoader::run(const std::shared_ptr<const XamlIR>& shared, usize first, usize last,
                               XamlLoadTarget& target, const XamlLoadOptions& options,
                               XamlHandle templatedParent)
{
    const XamlIR& ir = *shared;
    XamlLoadResult result;
    // What happens when an object's instructions end: its style, then its explicit template.
    struct Open
    {
        XamlHandle object = 0;
        bool explicitStyle = false;
        std::optional<u32> templateKey;
        std::optional<usize> inlineTemplate;
    };
    std::vector<Open> stack;
    // Rollback state: the outermost objects this load created, the existing root's previous local
    // values of every property the load wrote on it, and whether it got resources.
    std::vector<XamlHandle> created;
    std::vector<std::pair<const XamlIRProperty*, std::optional<PropertyValue>>> rootValues;
    bool rootResources = false;
    // Binding options read for the next kSetBinding; ElementName bindings wait for the whole name
    // scope (an element may name one declared after it).
    XamlBindingOptions binding;
    struct Deferred
    {
        XamlHandle object;
        const XamlIRInstruction* in;
        XamlBindingOptions options;
    };
    std::vector<Deferred> byName;
    // Visual states connect once the whole name scope exists: their targets may come later.
    std::vector<std::pair<XamlHandle, usize>> visualStates;
    const auto remember = [&](const XamlIRInstruction& in)
    {
        if (stack.size() != 1 || !options.existingRoot)
            return;
        const XamlIRProperty* property = &ir.properties[in.a];
        for (const auto& entry : rootValues)
            if (entry.first == property)
                return;
        rootValues.emplace_back(property, target.localValue(stack.back().object, *property));
    };
    const auto fail = [&](const XamlIRInstruction& in, std::string message)
    {
        result.error = std::format("{}:{}:{}: {}", ir.sourceName, in.line, in.column, message);
        for (auto it = created.rbegin(); it != created.rend(); ++it)
            target.discard(*it);
        for (auto it = rootValues.rbegin(); it != rootValues.rend(); ++it)
            target.restoreLocal(options.existingRoot, *it->first, it->second);
        if (rootResources)
            target.clearResources(options.existingRoot);
        result.root = 0;
        result.names.clear();
        result.ok = false;
        return result;
    };
    const auto property = [&](const XamlIRInstruction& in, std::string error)
    { return ir.properties[in.a].name + ": " + error; };

    for (usize i = first; i < last; ++i)
    {
        const auto& in = ir.instructions[i];
        XamlHandle current = stack.empty() ? 0 : stack.back().object;
        switch (in.op)
        {
        case XamlIROp::kBeginObject:
        {
            const XamlIRType& type = ir.types[in.a];
            // Metadata first (R07.1): no object of a type the runtime does not know.
            if (!m_types.find(type.id))
                return fail(in, "no reflection metadata for type '" + type.className + "'");
            if (!m_types.isA(type.id, typeIdFor(kUIElementClassName)))
                return fail(in, "'" + type.className + "' is not a UIElement");
            XamlHandle object = 0;
            if (stack.empty() && options.existingRoot)
            {
                if (!target.isOfType(options.existingRoot, type))
                    return fail(in, "the existing root is not a '" + type.className + "'");
                object = options.existingRoot;
            }
            else
            {
                std::string error;
                object = target.create(type, stack.empty() ? options.parent : current, error);
                if (!object)
                    return fail(in, "cannot create '" + type.className + "': " + error);
                // Discarding an outermost created object removes its subtree with it.
                if (stack.empty() || (stack.size() == 1 && options.existingRoot))
                    created.push_back(object);
            }
            if (stack.empty())
                result.root = object;
            stack.push_back({object});
            break;
        }
        case XamlIROp::kEndObject:
        {
            const Open open = stack.back();
            if (!open.explicitStyle)
                if (auto error = target.applyImplicitStyle(open.object); !error.empty())
                    return fail(in, "implicit style: " + error);
            if (open.templateKey)
                if (auto error = target.applyTemplate(open.object, ir.strings[*open.templateKey]);
                    !error.empty())
                    return fail(in, "Template: " + error);
            if (open.inlineTemplate)
                if (auto error = target.applyTemplate(open.object, shared, *open.inlineTemplate);
                    !error.empty())
                    return fail(in, "Template: " + error);
            stack.pop_back();
            break;
        }
        case XamlIROp::kSetValue:
            remember(in);
            if (auto error = target.setValue(current, ir.properties[in.a], ir.values[in.b]); !error.empty())
                return fail(in, property(in, error));
            break;
        case XamlIROp::kBeginVisualStates:
            visualStates.emplace_back(current, i);
            i = xamlBlockEnd(ir, i);
            break;
        case XamlIROp::kBeginDataTemplate:
            if (auto error = target.setItemTemplate(current, shared, i); !error.empty())
                return fail(in, "ItemTemplate: " + error);
            i = xamlBlockEnd(ir, i);
            break;
        case XamlIROp::kSetItemTemplate:
            if (auto error = target.setItemTemplate(current, ir.strings[in.b]); !error.empty())
                return fail(in, "ItemTemplate: " + error);
            break;
        case XamlIROp::kSetItemXBind:
            if (auto error = target.setItemXBind(current, ir.properties[in.a], in.b, in.c); !error.empty())
                return fail(in, property(in, "x:Bind: " + error));
            break;
        case XamlIROp::kSetItemsSource:
            if (auto error = target.setItemsSource(current, in.b, in.c); !error.empty())
                return fail(in, "ItemsSource: " + error);
            break;
        case XamlIROp::kSetXBind:
            remember(in);
            if (auto error = target.setXBind(current, ir.properties[in.a], in.b, in.c); !error.empty())
                return fail(in, property(in, "x:Bind: " + error));
            break;
        case XamlIROp::kSetUid:
            if (auto error = target.applyUid(current, ir.strings[in.b]); !error.empty())
                return fail(in, "x:Uid '" + ir.strings[in.b] + "': " + error);
            break;
        case XamlIROp::kBindElementName:
            binding.elementName = ir.strings[in.b];
            break;
        case XamlIROp::kBindRelativeSource:
            binding.relativeSource = static_cast<XamlRelativeSource>(in.c);
            break;
        case XamlIROp::kBindConverter:
            binding.converter = ir.strings[in.b];
            if (in.c != kXamlNoIndex)
                binding.converterParameter = ir.strings[in.c];
            break;
        case XamlIROp::kBindFallback:
            binding.fallback = ir.values[in.b];
            break;
        case XamlIROp::kSetBinding:
        {
            remember(in);
            XamlBindingOptions options = std::exchange(binding, {});
            if (!options.elementName.empty())
            {
                byName.push_back({current, &in, std::move(options)});
                break;
            }
            XamlHandle source = 0;
            if (options.relativeSource == XamlRelativeSource::kSelf)
                source = current;
            else if (options.relativeSource == XamlRelativeSource::kTemplatedParent)
            {
                if (!templatedParent)
                    return fail(in, "RelativeSource TemplatedParent outside a template instance");
                source = templatedParent;
            }
            if (auto error =
                    target.setBinding(current, ir.properties[in.a], ir.strings[in.b], in.c, options, source);
                !error.empty())
                return fail(in, property(in, error));
            break;
        }
        case XamlIROp::kSetResource:
            remember(in);
            if (auto error = target.setResource(current, ir.properties[in.a], ir.strings[in.b], in.c != 0);
                !error.empty())
                return fail(in, property(in, error));
            break;
        case XamlIROp::kSetName:
            if (!result.names.emplace(ir.strings[in.b], current).second)
                return fail(in, "x:Name '" + ir.strings[in.b] + "' is used twice in one name scope");
            break;
        case XamlIROp::kAddEvent:
        {
            const auto& handlerName = ir.strings[in.c];
            std::function<void()> handler = options.elementConnector
                                                ? options.elementConnector(handlerName, current)
                                            : options.connector ? options.connector(handlerName)
                                                                : nullptr;
            if (!handler)
                return fail(in, "no handler '" + handlerName + "' for event " + ir.strings[in.b]);
            if (auto error = target.addEvent(current, ir.strings[in.b], std::move(handler)); !error.empty())
                return fail(in, ir.strings[in.b] + ": " + error);
            break;
        }
        case XamlIROp::kBeginResources:
        {
            const usize end = xamlBlockEnd(ir, i);
            if (auto error = target.setResources(current, shared, i, end); !error.empty())
                return fail(in, "Resources: " + error);
            rootResources = rootResources || (stack.size() == 1 && options.existingRoot);
            i = end;
            break;
        }
        case XamlIROp::kApplyStyle:
            stack.back().explicitStyle = true;
            if (auto error = target.applyStyle(current, ir.strings[in.b]); !error.empty())
                return fail(in, "Style: " + error);
            break;
        case XamlIROp::kSetTemplate:
            stack.back().templateKey = in.b;
            break;
        case XamlIROp::kBeginTemplate:
            // An inline <Control.Template>: applied when the object is complete, replayed per instance.
            stack.back().inlineTemplate = i;
            i = xamlBlockEnd(ir, i);
            break;
        case XamlIROp::kSetTemplateBinding:
            if (!templatedParent)
                return fail(in, "TemplateBinding outside a template instance");
            if (auto error =
                    target.templateBind(current, ir.properties[in.a], templatedParent, ir.properties[in.b]);
                !error.empty())
                return fail(in, property(in, error));
            break;
        default:
            // Dictionary-only instructions never reach here: validate() keeps them inside Resources
            // blocks, which are skipped whole above.
            return fail(in, "unexpected instruction outside a dictionary");
        }
    }
    for (const auto& [object, begin] : visualStates)
        if (auto error = target.setVisualStates(object, templatedParent ? templatedParent : object, ir, begin,
                                                xamlBlockEnd(ir, begin), result.names);
            !error.empty())
            return fail(ir.instructions[begin], "visual states: " + error);
    for (const auto& d : byName)
    {
        const auto source = result.names.find(d.options.elementName);
        if (source == result.names.end())
            return fail(*d.in, "ElementName '" + d.options.elementName + "' is not in this name scope");
        if (auto error = target.setBinding(d.object, ir.properties[d.in->a], ir.strings[d.in->b], d.in->c,
                                           d.options, source->second);
            !error.empty())
            return fail(*d.in, property(*d.in, error));
    }
    result.ok = true;
    return result;
}

} // namespace velora
