#pragma once
#include <functional>
#include <memory>
#include <string>
#include <velora/runtime/Framework.hpp>

namespace velora
{
// Content is recreated per opening in the existing UI tree; no second native window/runtime.
// Factories return a fresh top-level root and transfer ownership on return. Throwing factories
// must roll back their own partial work (compiled component loading already does this).
class Flyout : public std::enable_shared_from_this<Flyout>
{
  public:
    using ContentFactory = std::function<ElementId(Framework&)>;
    explicit Flyout(ContentFactory factory);
    virtual ~Flyout();
    Flyout(const Flyout&) = delete;
    Flyout& operator=(const Flyout&) = delete;
    bool showAt(Framework& ui, ElementId anchor, std::function<void(bool)> changed = {},
                ElementId passThrough = 0);
    void hide();
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool isOpenAt(const Framework& ui, ElementId anchor) const noexcept;
    [[nodiscard]] ElementId anchor() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

  private:
    ContentFactory m_factory;
    Framework* m_ui = nullptr;
    std::weak_ptr<void> m_lifetime;
    ElementId m_root = 0, m_anchor = 0;
    std::string m_error;
    bool m_opening = false;
};
} // namespace velora
