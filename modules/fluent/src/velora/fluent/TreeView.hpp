// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
#include <velora/fluent/TreeViewNode.hpp>
namespace velora
{
class TreeView final : public UIElement, public std::enable_shared_from_this<TreeView>
{
  public:
    TreeView();
    ~TreeView() override;
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    void setRootNodes(std::vector<std::shared_ptr<TreeViewNode>>);
    const std::vector<std::shared_ptr<TreeViewNode>>& rootNodes() const noexcept;
    static const PropertyDescriptor& selectedNodeProperty();
    std::shared_ptr<TreeViewNode> selectedNode() const;
    PropertySetResult setSelectedNode(std::shared_ptr<TreeViewNode>);
    std::string selectionMode() const;
    PropertySetResult setSelectionMode(std::string);
    void expand(const std::shared_ptr<TreeViewNode>&);
    void collapse(const std::shared_ptr<TreeViewNode>&);
    std::shared_ptr<UIElement> containerFromNode(const std::shared_ptr<TreeViewNode>&) const;
    std::shared_ptr<TreeViewNode> nodeFromContainer(const UIElement&) const;
    u64 addSelectionChanged(std::function<void(std::shared_ptr<TreeViewNode>)>);
    u64 addItemInvoked(std::function<void(std::shared_ptr<TreeViewNode>)>);
    u64 addExpanding(std::function<void(std::shared_ptr<TreeViewNode>)>);
    u64 addCollapsed(std::function<void(std::shared_ptr<TreeViewNode>)>);
    bool removeHandler(u64);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    friend class TreeViewNode;
    static const PropertyDescriptor& selectionModeProperty();
    void beginModelUpdate();
    void endModelUpdate();
    void nodeChanged(const std::shared_ptr<TreeViewNode>&, const PropertyDescriptor&);
    void modelChanged();
    void refreshRows();
    void invokeNode(const std::shared_ptr<TreeViewNode>&);
    void focusNode(std::shared_ptr<TreeViewNode>);
    bool owns(const std::shared_ptr<TreeViewNode>&) const;
    u64 nextToken();
    struct Row
    {
        std::shared_ptr<TreeViewNode> node;
        ElementId root = 0, button = 0;
    };
    std::shared_ptr<TreeViewNode> m_root;
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_rowsHost = 0;
    bool m_rebuilding = false, m_dirty = false;
    i32 m_modelUpdates = 0;
    std::vector<Row> m_rows;
    std::vector<EventSubscription> m_events;
    u64 m_nextToken = 1;
    std::map<u64, std::function<void(std::shared_ptr<TreeViewNode>)>> m_invoked, m_expanding, m_collapsed;
};
} // namespace velora
