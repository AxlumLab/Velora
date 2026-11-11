// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 TreeView.cpp and ViewModel.cpp: visible preorder and single selection.
#include "FluentTypes.hpp"
#include <algorithm>
#include <array>
#include <exception>
#include <stdexcept>
#include <utility>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/TreeView.hpp>
namespace velora
{
const XamlTypeDeclaration& TreeView::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.TreeView");
}
const PropertyDescriptor& TreeView::selectionModeProperty()
{
    static PropertyRegistry r;
    static const auto& p = r.define("TreeView.SelectionMode", "Single", PropertyFlags::kPaint);
    return p;
}
const PropertyDescriptor& TreeView::selectedNodeProperty()
{
    static PropertyRegistry r;
    static const auto& p = r.define("TreeView.SelectedNode", ObjectRef{}, PropertyFlags::kPaint);
    return p;
}
std::string TreeView::className() const
{
    return "Velora.Fluent.TreeView";
}
std::string TreeView::registerType(Framework& ui)
{
    const std::array properties{&selectionModeProperty()};
    return ui.registerSourceControl(declaration(), [] { return createElement<TreeView>(); }, properties);
}
TreeView::TreeView() : UIElement(ElementKind::kUserControl), m_root(std::make_shared<TreeViewNode>())
{
    m_root->m_hidden = true;
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.id == selectionModeProperty().id && selectionMode() == "None")
                setSelectedNode(nullptr);
            if (p.id == selectedNodeProperty().id || p.id == selectionModeProperty().id)
                modelChanged();
        });
}
TreeView::~TreeView()
{
    if (!isOwnerThread())
        std::terminate();
    m_root->bindOwner({}, -1);
}
const std::vector<std::shared_ptr<TreeViewNode>>& TreeView::rootNodes() const noexcept
{
    return m_root->children();
}
void TreeView::setRootNodes(std::vector<std::shared_ptr<TreeViewNode>> nodes)
{
    if (!isOwnerThread() || m_rebuilding || weak_from_this().expired())
        throw std::logic_error("TreeView roots require shared ownership on the UI thread");
    m_root->bindOwner(weak_from_this(), -1);
    m_root->setChildren(std::move(nodes));
}
std::string TreeView::selectionMode() const
{
    return getValue(selectionModeProperty()).asString();
}
PropertySetResult TreeView::setSelectionMode(std::string value)
{
    return setValue(selectionModeProperty(), std::move(value));
}
std::shared_ptr<TreeViewNode> TreeView::selectedNode() const
{
    const auto& ref = getValue(selectedNodeProperty()).asObject();
    return ref ? std::shared_ptr<TreeViewNode>{ref.owner, static_cast<TreeViewNode*>(ref.object)} : nullptr;
}
PropertySetResult TreeView::setSelectedNode(std::shared_ptr<TreeViewNode> node)
{
    return setValue(selectedNodeProperty(), ObjectRef::share<TreeViewNode>(std::move(node)));
}
bool TreeView::owns(const std::shared_ptr<TreeViewNode>& node) const
{
    return node && node != m_root && node->m_owner.lock().get() == this;
}
bool TreeView::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == selectionModeProperty().id)
        return !m_rebuilding && value.isString() &&
               (value.asString() == "None" || value.asString() == "Single");
    if (p.id != selectedNodeProperty().id)
        return true;
    if (m_rebuilding || !value.isObject())
        return false;
    const auto& ref = value.asObject();
    if (!ref)
        return true;
    if (selectionMode() == "None" || !ref.owner || ref.owner.get() != ref.object || !ref.type ||
        *ref.type != typeid(TreeViewNode))
        return false;
    return owns(std::shared_ptr<TreeViewNode>{ref.owner, static_cast<TreeViewNode*>(ref.object)});
}
void TreeView::expand(const std::shared_ptr<TreeViewNode>& node)
{
    if (!isOwnerThread() || !owns(node))
        throw std::invalid_argument("Expand requires a node in this TreeView");
    node->setIsExpanded(true);
}
void TreeView::collapse(const std::shared_ptr<TreeViewNode>& node)
{
    if (!isOwnerThread() || !owns(node))
        throw std::invalid_argument("Collapse requires a node in this TreeView");
    node->setIsExpanded(false);
}
void TreeView::beginModelUpdate()
{
    ++m_modelUpdates;
}
void TreeView::endModelUpdate()
{
    if (--m_modelUpdates == 0 && m_dirty)
        refreshRows();
}
void TreeView::modelChanged()
{
    m_dirty = true;
    if (m_modelUpdates == 0)
        refreshRows();
}
void TreeView::nodeChanged(const std::shared_ptr<TreeViewNode>& node, const PropertyDescriptor& p)
{
    if (m_rebuilding)
        return;
    const auto keepAlive = shared_from_this();
    beginModelUpdate();
    try
    {
        if (p.id == TreeViewNode::isExpandedProperty().id && owns(node))
        {
            const bool expanded = node->isExpanded();
            const auto handlers = expanded ? m_expanding : m_collapsed;
            for (const auto& [token, handler] : handlers)
                if ((expanded ? m_expanding : m_collapsed).contains(token))
                    handler(node);
        }
        modelChanged();
    }
    catch (...)
    {
        endModelUpdate();
        throw;
    }
    endModelUpdate();
}
void TreeView::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    m_root->bindOwner(weak_from_this(), -1);
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded =
        loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/TreeViewView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("TreeView template: " + loaded.error);
    m_rowsHost = ui.tree().element(*loaded.names.at("Rows"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root), {});
    refreshRows();
}
void TreeView::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_rowsHost = 0;
    m_events.clear();
    m_rows.clear();
}
std::shared_ptr<UIElement> TreeView::containerFromNode(const std::shared_ptr<TreeViewNode>& node) const
{
    if (!isOwnerThread() || !m_ui)
        return {};
    for (const auto& row : m_rows)
        if (row.node == node)
            return std::dynamic_pointer_cast<UIElement>(m_ui->tree().object(row.root));
    return {};
}
std::shared_ptr<TreeViewNode> TreeView::nodeFromContainer(const UIElement& container) const
{
    if (!isOwnerThread() || !m_ui)
        return {};
    const auto id = m_ui->tree().element(container);
    for (const auto& row : m_rows)
        if (m_ui->isWithin(id, row.root))
            return row.node;
    return {};
}
void TreeView::focusNode(std::shared_ptr<TreeViewNode> node)
{
    while (m_ui && node && node != m_root)
    {
        for (const auto& row : m_rows)
            if (row.node == node)
            {
                m_ui->focus(row.button);
                return;
            }
        node = node->parent();
    }
    if (m_ui && !m_rows.empty())
        m_ui->focus(m_rows.front().button);
}
void TreeView::refreshRows()
{
    if (m_rebuilding)
        return;
    const auto keepAlive = weak_from_this().lock();
    const auto selected = selectedNode();
    if (selected && !owns(selected))
    {
        ++m_modelUpdates;
        setSelectedNode(nullptr);
        --m_modelUpdates;
    }
    m_dirty = false;
    if (!m_ui || !m_rowsHost)
        return;
    std::shared_ptr<TreeViewNode> focused;
    if (const auto* object = m_ui->tree().uiElement(m_ui->focused()))
        focused = nodeFromContainer(*object);
    m_rebuilding = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_rebuilding};
    auto* ui = m_ui;
    m_events.clear();
    m_rows.clear();
    const auto old = ui->tree().children(m_rowsHost);
    for (const auto id : old)
        if (m_ui)
            ui->tree().remove(id);
    std::vector<std::shared_ptr<TreeViewNode>> pending(rootNodes().rbegin(), rootNodes().rend()), visible;
    while (!pending.empty())
    {
        const auto node = std::move(pending.back());
        pending.pop_back();
        visible.push_back(node);
        if (node->isExpanded())
            pending.insert(pending.end(), node->children().rbegin(), node->children().rend());
    }
    auto tabNode = selectedNode();
    while (tabNode && std::find(visible.begin(), visible.end(), tabNode) == visible.end())
        tabNode = tabNode->parent();
    if (!tabNode && !visible.empty())
        tabNode = visible.front();
    const auto weak = weak_from_this();
    try
    {
        for (const auto& node : visible)
        {
            if (!m_ui)
                return;
            ComponentLoadOptions options;
            options.parent = ui->tree().uiElement(m_rowsHost);
            const auto loaded = loadComponent(
                *ui, *ResourceUri::parse("ms-appx-framework:///Controls/TreeViewRowView.vxbf"), options);
            if (!loaded.ok)
                throw std::runtime_error("TreeView row: " + loaded.error);
            const auto root = ui->tree().element(*loaded.root);
            const auto expander = ui->tree().element(*loaded.names.at("Expander"));
            const auto button = ui->tree().element(*loaded.names.at("ItemButton"));
            m_rows.push_back({node, root, button});
            ui->set(root, ui->props().Margin, Thickness{static_cast<f64>(node->depth()) * 20, 0, 0, 0});
            ui->set(expander, ui->props().Content,
                    node->hasChildren() ? (node->isExpanded() ? "\xEE\x9C\x8D" : "\xEE\x9D\xAC") : "");
            ui->set(expander, ui->props().IsEnabled, node->hasChildren());
            ui->set(button, ui->props().Content, node->content());
            ui->set(button, ui->props().FontSize, node == selectedNode() ? 18.0 : 14.0);
            ui->set(button, ui->props().IsTabStop, node == tabNode);
            const std::weak_ptr<TreeViewNode> weakNode = node;
            m_events.push_back(ui->subscribeEvent(expander, ui->events().Click,
                                                  [weak, weakNode](ElementId, RoutedEventArgs&)
                                                  {
                                                      const auto owner = weak.lock();
                                                      const auto node = weakNode.lock();
                                                      if (owner && node)
                                                      {
                                                          owner->focusNode(node);
                                                          node->setIsExpanded(!node->isExpanded());
                                                      }
                                                  }));
            m_events.push_back(ui->subscribeEvent(button, ui->events().Click,
                                                  [weak, weakNode](ElementId, RoutedEventArgs&)
                                                  {
                                                      const auto owner = weak.lock();
                                                      const auto node = weakNode.lock();
                                                      if (owner && node)
                                                          owner->invokeNode(node);
                                                  }));
        }
    }
    catch (...)
    {
        m_events.clear();
        m_rows.clear();
        if (m_ui)
        {
            const auto rows = ui->tree().children(m_rowsHost);
            for (const auto id : rows)
                if (m_ui)
                    ui->tree().remove(id);
        }
        throw;
    }
    if (focused)
        focusNode(focused);
}
void TreeView::invokeNode(const std::shared_ptr<TreeViewNode>& node)
{
    if (!m_ui || !owns(node))
        return;
    const auto keepAlive = shared_from_this();
    if (selectionMode() == "Single")
        setSelectedNode(node);
    if (m_ui)
        focusNode(node);
    const auto handlers = m_invoked;
    for (const auto& [token, handler] : handlers)
        if (m_ui && m_invoked.contains(token))
            handler(node);
}
void TreeView::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (!m_ui || args.ctrl || args.alt || m_rows.empty())
        return;
    const auto* source = m_ui->tree().uiElement(m_ui->focused());
    const auto node = source ? nodeFromContainer(*source) : nullptr;
    if (!node)
        return;
    usize index = 0;
    for (usize i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].node == node)
            index = i;
    if (args.key == Key::kEnter || args.key == Key::kSpace)
        invokeNode(node);
    else if (args.key == Key::kRight)
    {
        if (node->hasChildren() && !node->isExpanded())
            expand(node);
        else if (!node->children().empty())
            focusNode(node->children().front());
    }
    else if (args.key == Key::kLeft)
    {
        if (node->hasChildren() && node->isExpanded())
            collapse(node);
        else if (const auto parent = node->parent(); parent && parent != m_root)
            focusNode(parent);
    }
    else if (args.key == Key::kHome)
        focusNode(m_rows.front().node);
    else if (args.key == Key::kEnd)
        focusNode(m_rows.back().node);
    else if (args.key == Key::kDown)
        focusNode(m_rows[std::min(index + 1, m_rows.size() - 1)].node);
    else if (args.key == Key::kUp)
        focusNode(m_rows[index ? index - 1 : 0].node);
    else
        return;
    args.handled = true;
}
u64 TreeView::addSelectionChanged(std::function<void(std::shared_ptr<TreeViewNode>)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [this, handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                             const PropertyValue&, const PropertyValue&)
        {
            if (p.id == selectedNodeProperty().id)
                handler(selectedNode());
        });
}
u64 TreeView::nextToken()
{
    if (!isOwnerThread())
        throw std::logic_error("TreeView events belong to its UI thread");
    if (!m_nextToken)
        throw std::overflow_error("TreeView event tokens exhausted");
    return m_nextToken++;
}
u64 TreeView::addItemInvoked(std::function<void(std::shared_ptr<TreeViewNode>)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_invoked.emplace(token, std::move(handler));
    return token;
}
u64 TreeView::addExpanding(std::function<void(std::shared_ptr<TreeViewNode>)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_expanding.emplace(token, std::move(handler));
    return token;
}
u64 TreeView::addCollapsed(std::function<void(std::shared_ptr<TreeViewNode>)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_collapsed.emplace(token, std::move(handler));
    return token;
}
bool TreeView::removeHandler(u64 token)
{
    return isOwnerThread() &&
           (m_invoked.erase(token) || m_expanding.erase(token) || m_collapsed.erase(token));
}
} // namespace velora
