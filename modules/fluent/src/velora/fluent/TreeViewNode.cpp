// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 TreeViewNode.cpp/.idl: weak parent, depth and child/expansion state.
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <velora/fluent/TreeView.hpp>
#include <velora/text/Encoding.hpp>
namespace velora
{
namespace
{
const ReadOnlyPropertyKey& depthKey()
{
    static PropertyRegistry registry;
    static const auto key = []
    {
        PropertyDescriptor p;
        p.name = "TreeViewNode.Depth";
        p.defaultValue = i64{-1};
        return registry.defineReadOnly(std::move(p));
    }();
    return key;
}
const ReadOnlyPropertyKey& childrenKey()
{
    static PropertyRegistry registry;
    static const auto key = []
    {
        PropertyDescriptor p;
        p.name = "TreeViewNode.HasChildren";
        p.defaultValue = false;
        return registry.defineReadOnly(std::move(p));
    }();
    return key;
}
} // namespace
TreeViewNode::TreeViewNode()
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.id == hasUnrealizedChildrenProperty().id)
                updateHasChildren();
            if (p.id == contentProperty().id || p.id == isExpandedProperty().id ||
                p.id == hasChildrenProperty().id)
                if (const auto owner = m_owner.lock())
                    owner->nodeChanged(shared_from_this(), p);
        });
}
TreeViewNode::~TreeViewNode()
{
    if (!isOwnerThread())
        std::terminate();
    for (const auto& child : m_children)
    {
        child->m_parent.reset();
        try
        {
            child->bindOwner({}, -1);
        }
        catch (...)
        {
        }
    }
}
const PropertyDescriptor& TreeViewNode::contentProperty()
{
    static PropertyRegistry r;
    static const auto& p = r.define("TreeViewNode.Content", "", PropertyFlags::kPaint);
    return p;
}
const PropertyDescriptor& TreeViewNode::isExpandedProperty()
{
    static PropertyRegistry r;
    static const auto& p = r.define("TreeViewNode.IsExpanded", false, PropertyFlags::kPaint);
    return p;
}
const PropertyDescriptor& TreeViewNode::hasUnrealizedChildrenProperty()
{
    static PropertyRegistry r;
    static const auto& p = r.define("TreeViewNode.HasUnrealizedChildren", false, PropertyFlags::kPaint);
    return p;
}
const PropertyDescriptor& TreeViewNode::hasChildrenProperty()
{
    return childrenKey().property();
}
const PropertyDescriptor& TreeViewNode::depthProperty()
{
    return depthKey().property();
}
std::string TreeViewNode::content() const
{
    return getValue(contentProperty()).asString();
}
PropertySetResult TreeViewNode::setContent(std::string value)
{
    return setValue(contentProperty(), std::move(value));
}
bool TreeViewNode::isExpanded() const
{
    return getValue(isExpandedProperty()).asBool();
}
PropertySetResult TreeViewNode::setIsExpanded(bool value)
{
    return setValue(isExpandedProperty(), value);
}
bool TreeViewNode::hasUnrealizedChildren() const
{
    return getValue(hasUnrealizedChildrenProperty()).asBool();
}
PropertySetResult TreeViewNode::setHasUnrealizedChildren(bool value)
{
    return setValue(hasUnrealizedChildrenProperty(), value);
}
bool TreeViewNode::hasChildren() const
{
    return getValue(hasChildrenProperty()).asBool();
}
i32 TreeViewNode::depth() const
{
    return static_cast<i32>(getValue(depthProperty()).asInt());
}
std::shared_ptr<TreeViewNode> TreeViewNode::parent() const
{
    return m_parent.lock();
}
const std::vector<std::shared_ptr<TreeViewNode>>& TreeViewNode::children() const noexcept
{
    return m_children;
}
bool TreeViewNode::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (const auto owner = m_owner.lock(); owner && owner->m_rebuilding)
        return false;
    if (p.id == isExpandedProperty().id || p.id == hasUnrealizedChildrenProperty().id)
        return value.isBool();
    if (p.id != contentProperty().id)
        return true;
    if (!value.isString())
        return false;
    try
    {
        utf8ToUtf16(value.asString());
    }
    catch (const std::exception&)
    {
        return false;
    }
    return true;
}
void TreeViewNode::updateHasChildren()
{
    setValue(childrenKey(), !m_children.empty() || hasUnrealizedChildren());
}
void TreeViewNode::bindOwner(std::weak_ptr<TreeView> owner, i32 depth)
{
    m_owner = owner;
    setValue(depthKey(), static_cast<i64>(depth));
    const auto children = m_children;
    for (const auto& child : children)
        child->bindOwner(owner, depth + 1);
}
void TreeViewNode::setChildren(std::vector<std::shared_ptr<TreeViewNode>> children)
{
    if (!isOwnerThread() || m_editing || weak_from_this().expired())
        throw std::logic_error("Tree nodes require shared ownership on their UI thread");
    const auto keepAlive = shared_from_this();
    const auto owner = m_owner.lock();
    if (owner && owner->m_rebuilding)
        throw std::logic_error("Cannot edit nodes while TreeView realizes rows");
    std::unordered_set<const TreeViewNode*> unique;
    for (const auto& child : children)
    {
        if (!child || child->m_hidden || !child->isOwnerThread() || !unique.insert(child.get()).second ||
            (child->parent() && child->parent().get() != this) ||
            (!child->m_owner.expired() && child->parent().get() != this))
            throw std::invalid_argument("Tree children require distinct nodes without an existing parent");
        for (auto ancestor = keepAlive; ancestor; ancestor = ancestor->parent())
            if (ancestor == child)
                throw std::invalid_argument("Tree children cannot create a cycle");
        std::vector<std::pair<std::shared_ptr<TreeViewNode>, i32>> pending{{child, depth() + 1}};
        while (!pending.empty())
        {
            auto [node, level] = std::move(pending.back());
            pending.pop_back();
            if (level > 512)
                throw std::length_error("TreeView's initial node-depth limit is 512");
            for (const auto& descendant : node->m_children)
                pending.emplace_back(descendant, level + 1);
        }
    }
    m_editing = true;
    if (owner)
        owner->beginModelUpdate();
    try
    {
        const auto previous = std::exchange(m_children, std::move(children));
        for (const auto& child : previous)
            if (!unique.contains(child.get()))
            {
                child->m_parent.reset();
                child->bindOwner({}, -1);
            }
        for (const auto& child : m_children)
        {
            child->m_parent = keepAlive;
            child->bindOwner(m_owner, depth() + 1);
        }
        updateHasChildren();
        if (owner)
            owner->modelChanged();
    }
    catch (...)
    {
        m_editing = false;
        if (owner)
            owner->endModelUpdate();
        throw;
    }
    m_editing = false;
    if (owner)
        owner->endModelUpdate();
}
void TreeViewNode::appendChild(std::shared_ptr<TreeViewNode> child)
{
    if (!isOwnerThread())
        throw std::logic_error("Tree children belong to their UI thread");
    auto result = m_children;
    result.push_back(std::move(child));
    setChildren(std::move(result));
}
bool TreeViewNode::removeChild(const std::shared_ptr<TreeViewNode>& child)
{
    if (!isOwnerThread())
        throw std::logic_error("Tree children belong to their UI thread");
    auto result = m_children;
    const auto found = std::ranges::find(result, child);
    if (found == result.end())
        return false;
    result.erase(found);
    setChildren(std::move(result));
    return true;
}
} // namespace velora
