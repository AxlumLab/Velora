// Short, allocator-aware aliases for STL collections. No custom container ABI.
#pragma once

#include <deque>
#include <list>
#include <map>
#include <memory_resource>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <velora/globals.hpp>

namespace velora
{
template <typename T> using Array = std::pmr::vector<T>;

template <typename T> using Deque = std::pmr::deque<T>;

template <typename T> using List = std::pmr::list<T>;

template <typename Key, typename Value, typename Compare = std::less<Key>>
using Map = std::pmr::map<Key, Value, Compare>;

template <typename Key, typename Value, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
using HashMap = std::pmr::unordered_map<Key, Value, Hash, Equal>;

template <typename Key, typename Compare = std::less<Key>> using Set = std::pmr::set<Key, Compare>;

template <typename Key, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
using HashSet = std::pmr::unordered_set<Key, Hash, Equal>;

using String = std::pmr::string;

// PMR containers do not own their memory_resource; keep that resource alive until all
// associated containers and allocations are released. Use ordinary STL containers when
// no explicit allocation resource is needed.
}
