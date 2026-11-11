// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <velora/runtime/property/DependencyObject.hpp>
namespace velora
{
class TreeView;
class TreeViewNode final : public DependencyObject, public std::enable_shared_from_this<TreeViewNode>
{
  public:
    TreeViewNode();
    ~TreeViewNode() override;
    static const PropertyDescriptor& contentProperty();
    static const PropertyDescriptor& isExpandedProperty();
    static const PropertyDescriptor& hasUnrealizedChildrenProperty();
    static const PropertyDescriptor& hasChildrenProperty();
    static const PropertyDescriptor& depthProperty();
    std::string content() const;
    PropertySetResult setContent(std::string);
    bool isExpanded() const;
    PropertySetResult setIsExpanded(bool);
    bool hasUnrealizedChildren() const;
    PropertySetResult setHasUnrealizedChildren(bool);
    bool hasChildren() const;
    i32 depth() const;
    std::shared_ptr<TreeViewNode> parent() const;
    const std::vector<std::shared_ptr<TreeViewNode>>& children() const noexcept;
    void setChildren(std::vector<std::shared_ptr<TreeViewNode>>);
    void appendChild(std::shared_ptr<TreeViewNode>);
    bool removeChild(const std::shared_ptr<TreeViewNode>&);

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    friend class TreeView;
    void bindOwner(std::weak_ptr<TreeView>, i32 depth);
    void updateHasChildren();
    std::weak_ptr<TreeViewNode> m_parent;
    std::weak_ptr<TreeView> m_owner;
    std::vector<std::shared_ptr<TreeViewNode>> m_children;
    bool m_editing = false, m_hidden = false;
};
} // namespace velora
