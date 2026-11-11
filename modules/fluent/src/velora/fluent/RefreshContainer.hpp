// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/RefreshVisualizer.hpp>
namespace velora
{
class RefreshContainer final : public ContentElement, public std::enable_shared_from_this<RefreshContainer>
{
  public:
    RefreshContainer();
    ~RefreshContainer() override;
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& pullDirectionProperty();
    static const PropertyDescriptor& visualizerProperty();
    RefreshPullDirection pullDirection() const;
    PropertySetResult setPullDirection(RefreshPullDirection);
    std::shared_ptr<RefreshVisualizer> visualizer() const;
    PropertySetResult setVisualizer(std::shared_ptr<RefreshVisualizer>);
    void requestRefresh();
    u64 addRefreshRequested(std::function<void(const RefreshRequestedEventArgs&)>);
    bool removeRefreshRequested(u64);
    std::vector<std::shared_ptr<UIElement>> ownedContentObjects() const override;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;

  protected:
    std::string_view contentTemplateUri() const override;
    std::string_view contentHostName() const override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void syncVisualizer();
    void syncDirection();
    void unbindVisualizer() noexcept;
    std::weak_ptr<RefreshVisualizer> m_bound;
    u64 m_forwardToken = 0, m_nextToken = 1;
    ElementId m_visualizerNode = 0;
    bool m_syncing = false;
    std::map<u64, std::function<void(const RefreshRequestedEventArgs&)>> m_requested;
};
} // namespace velora
