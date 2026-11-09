#pragma once
#include <memory>
#include <string>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/PersonPicture.hpp>

namespace Demo
{
class FluentPage
{
  public:
    void setStatusElement(const std::shared_ptr<velora::UIElement>& status)
    {
        m_status = status;
    }
    void record(std::string message)
    {
        if (const auto status = m_status.lock())
            status->setValue(*velora::elementProperties().props.Text, std::move(message));
    }
    void setPicture(const std::shared_ptr<velora::PersonPicture>& picture)
    {
        m_picture = picture;
    }
    void toggleGroup()
    {
        if (const auto picture = m_picture.lock())
            picture->setIsGroup(!picture->isGroup());
    }
    void incrementBadge()
    {
        if (const auto picture = m_picture.lock())
            picture->setBadgeNumber(picture->badgeNumber() + 1);
    }

  private:
    std::weak_ptr<velora::PersonPicture> m_picture;
    std::weak_ptr<velora::UIElement> m_status;
};
} // namespace Demo
