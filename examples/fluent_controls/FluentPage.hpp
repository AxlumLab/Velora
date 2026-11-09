#pragma once
#include <memory>
#include <velora/fluent/PersonPicture.hpp>

namespace Demo
{
class FluentPage
{
  public:
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
};
} // namespace Demo
