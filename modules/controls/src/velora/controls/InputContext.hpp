#pragma once

#include <velora/controls/LayoutTypes.hpp>
#include <velora/globals.hpp>

namespace velora
{
class Framework;

// A borrowed view of host input services for one virtual callback. Do not retain it.
class InputContext final
{
  public:
    InputContext(Framework& owner, u64 element) noexcept;
    [[nodiscard]] Box bounds() const;
    bool focus();
    bool capturePointer();
    [[nodiscard]] bool hasPointerCapture() const;
    void releasePointer();

  private:
    Framework& m_owner;
    u64 m_element;
};
} // namespace velora
