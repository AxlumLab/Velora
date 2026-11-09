#include "velora/reflect/Accessors.hpp"

#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace velora
{
namespace
{
std::unordered_map<std::type_index, const TypeAccessors*>& table()
{
    static std::unordered_map<std::type_index, const TypeAccessors*> map;
    return map;
}
std::mutex& tableMutex()
{
    static std::mutex m;
    return m;
}
} // namespace

bool registerAccessors(const TypeAccessors& accessors)
{
    std::scoped_lock lock(tableMutex());
    return table().emplace(*accessors.type, &accessors).second;
}

const TypeAccessors* findAccessors(const std::type_info& type)
{
    std::scoped_lock lock(tableMutex());
    const auto it = table().find(type);
    return it == table().end() ? nullptr : it->second;
}

} // namespace velora
