#include "velora/reflect/Reflect.hpp"

#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <velora/globals.hpp>

namespace velora
{
// The runtime is intentionally header-oriented: descriptors are assembled by
// generated metadata and consumers. This translation unit provides a stable
// library target and the shared value/descriptor utilities without importing Toolkit CoreKit.
static_assert(sizeof(ObjectHandle) == sizeof(u64));

u32 runtimeApiVersion() noexcept
{
    return 1;
}

i64 EnumDescriptor::value(std::string_view key) const
{
    for (const auto& item : values)
        if (item.first == key)
            return item.second;
    throw ReflectError("unknown enum name");
}
std::string_view EnumDescriptor::name(i64 value) const
{
    for (const auto& item : values)
        if (item.second == value)
            return item.first;
    throw ReflectError("unknown enum value");
}

ReflectValue TypeDescriptor::getField(const void* object, std::string_view key) const
{
    if (!object)
        throw ReflectError("invalid object");
    auto it = fields.find(std::string(key));
    if (it != fields.end())
        return it->second.get(object);
    if (base && constBaseCast)
        return base->getField(constBaseCast(object), key);
    throw ReflectError("unknown reflected field");
}
void TypeDescriptor::setField(void* object, std::string_view key, const ReflectValue& value) const
{
    if (!object)
        throw ReflectError("invalid object");
    auto it = fields.find(std::string(key));
    if (it != fields.end())
    {
        it->second.set(object, value);
        return;
    }
    if (base && baseCast)
    {
        base->setField(baseCast(object), key, value);
        return;
    }
    throw ReflectError("unknown reflected field");
}
ReflectValue TypeDescriptor::invoke(void* object, std::string_view key,
                                    std::span<const ReflectValue> args) const
{
    if (!object)
        throw ReflectError("invalid object");
    const MethodDescriptor* match = nullptr;
    for (const auto& [signature, method] : methods)
    {
        if (method.name != key && signature != key)
            continue;
        if (method.arity != args.size())
            continue;
        bool compatible = method.parameters.empty();
        if (!method.parameters.empty() && method.parameters.size() == args.size())
        {
            compatible = true;
            for (std::size_t i = 0; i < args.size(); ++i)
                compatible &= method.parameters[i] == args[i].kind();
        }
        if (compatible)
        {
            if (match)
                throw ReflectError("ambiguous reflected overload");
            match = &method;
        }
    }
    if (match)
        return match->invoke(object, args);
    if (base && baseCast)
        return base->invoke(baseCast(object), key, args);
    throw ReflectError("no matching reflected overload");
}

// Versioned, locale-independent values. Strings use hex bytes so embedded
// NUL/newline/UTF-8 survive; raw process addresses are deliberately excluded.
std::string serializeValue(const ReflectValue& value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "value 1 " << static_cast<int>(value.kind()) << ' ';
    switch (value.kind())
    {
    case ReflectValueKind::kEmpty:
        out << '-';
        break;
    case ReflectValueKind::kBool:
        out << (value.asBool() ? 1 : 0);
        break;
    case ReflectValueKind::kI64:
        out << value.asI64();
        break;
    case ReflectValueKind::kU64:
        out << value.asU64();
        break;
    case ReflectValueKind::kF64:
        if (!std::isfinite(value.asF64()))
            throw ReflectError("non-finite value cannot be serialized");
        out << std::setprecision(std::numeric_limits<double>::max_digits10) << value.asF64();
        break;
    case ReflectValueKind::kString:
        out << value.asString().size() << ':';
        for (unsigned char c : value.asString())
            out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c);
        break;
    case ReflectValueKind::kPointer:
        throw ReflectError("pointer serialization requires an application reference adapter");
    }
    return out.str();
}
ReflectValue deserializeValue(std::string_view text)
{
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    std::string tag;
    int version = 0, kind = -1;
    if (!(in >> tag >> version >> kind) || tag != "value" || version != 1)
        throw ReflectError("invalid value schema");
    ReflectValue result;
    switch (static_cast<ReflectValueKind>(kind))
    {
    case ReflectValueKind::kEmpty:
    {
        char c;
        if (!(in >> c) || c != '-')
            throw ReflectError("invalid empty value");
        break;
    }
    case ReflectValueKind::kBool:
    {
        int b;
        if (!(in >> b) || (b != 0 && b != 1))
            throw ReflectError("invalid bool");
        result = ReflectValue(b == 1);
        break;
    }
    case ReflectValueKind::kI64:
    {
        i64 n;
        if (!(in >> n))
            throw ReflectError("invalid signed value");
        result = ReflectValue(n);
        break;
    }
    case ReflectValueKind::kU64:
    {
        u64 n;
        in >> std::ws;
        if (in.peek() == '-' || !(in >> n))
            throw ReflectError("invalid unsigned value");
        result = ReflectValue(n);
        break;
    }
    case ReflectValueKind::kF64:
    {
        double n;
        if (!(in >> n) || !std::isfinite(n))
            throw ReflectError("invalid float");
        result = ReflectValue(n);
        break;
    }
    case ReflectValueKind::kString:
    {
        std::size_t size;
        char colon;
        if (!(in >> size >> colon) || colon != ':' || size > text.size() / 2)
            throw ReflectError("invalid string length");
        std::string value;
        value.reserve(size);
        auto digit = [](char c) -> int
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            return -1;
        };
        for (std::size_t i = 0; i < size; ++i)
        {
            char a, b;
            if (!in.get(a) || !in.get(b) || digit(a) < 0 || digit(b) < 0)
                throw ReflectError("invalid string bytes");
            value.push_back(static_cast<char>(digit(a) * 16 + digit(b)));
        }
        result = ReflectValue(std::move(value));
        break;
    }
    default:
        throw ReflectError("unsupported serialized value kind");
    }
    in >> std::ws;
    if (!in.eof())
        throw ReflectError("trailing value data");
    return result;
}

std::string TypeDescriptor::serialize(const void* object) const
{
    if (!object)
        throw ReflectError("invalid object");
    std::map<std::string, const FieldDescriptor*> members;
    std::function<void(const TypeDescriptor&)> collect = [&](const TypeDescriptor& type)
    {
        if (type.base)
            collect(*type.base);
        for (const auto& [name, field] : type.fields)
            members[name] = &field;
    };
    collect(*this);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "reflect-object 1 " << std::quoted(fullName) << '\n';
    for (const auto& [name, field] : members)
        if (field->serializable)
            out << std::quoted(name) << ' ' << serializeValue(getField(object, name)) << '\n';
    return out.str();
}
void TypeDescriptor::deserialize(void* object, std::string_view text) const
{
    if (!object)
        throw ReflectError("invalid object");
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    std::string line, tag, typeName;
    int version = 0;
    if (!std::getline(in, line))
        throw ReflectError("missing object header");
    std::istringstream header(line);
    if (!(header >> tag >> version >> std::quoted(typeName)) || tag != "reflect-object" || version != 1 ||
        typeName != fullName)
        throw ReflectError("object schema/type mismatch");
    header >> std::ws;
    if (!header.eof())
        throw ReflectError("trailing object header");
    struct Pending
    {
        std::string name;
        ReflectValue value;
    };
    std::vector<Pending> pending;
    std::set<std::string> names;
    while (std::getline(in, line))
    {
        std::istringstream row(line);
        std::string name, valueText;
        if (!(row >> std::quoted(name)) || !names.insert(name).second)
            throw ReflectError("invalid/duplicate object field");
        std::getline(row, valueText);
        auto value = deserializeValue(valueText);
        const TypeDescriptor* owner = this;
        while (owner && !owner->fields.contains(name))
            owner = owner->base.get();
        if (!owner)
            throw ReflectError("unknown serialized field");
        const auto& field = owner->fields.at(name);
        if (!field.serializable || !field.setter || field.kind != value.kind())
            throw ReflectError("serialized field is read-only/transient or has wrong type");
        pending.push_back({std::move(name), std::move(value)});
    }
    // Validate the whole document before any setter is invoked. Application
    // setter exceptions propagate; external side effects are not rolled back.
    for (const auto& item : pending)
        setField(object, item.name, item.value);
}
}
