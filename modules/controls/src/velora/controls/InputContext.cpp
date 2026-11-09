#include <velora/controls/InputContext.hpp>
#include <velora/runtime/Framework.hpp>

namespace velora
{
InputContext::InputContext(Framework& owner, u64 element) noexcept : m_owner(owner), m_element(element)
{
}

Box InputContext::bounds() const
{
    return m_owner.bounds(m_element);
}

bool InputContext::focus()
{
    if (!m_owner.tree().contains(m_element))
        return false;
    m_owner.focus(m_element);
    return m_owner.focused() == m_element;
}

bool InputContext::capturePointer()
{
    return m_owner.tree().capturePointer(m_element);
}

bool InputContext::hasPointerCapture() const
{
    return m_owner.tree().pointerCapture() == m_element;
}

void InputContext::releasePointer()
{
    if (m_owner.tree().pointerCapture() == m_element)
        m_owner.tree().releasePointer();
}
} // namespace velora
