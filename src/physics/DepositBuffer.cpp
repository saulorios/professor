#include "DepositBuffer.h"

#include <algorithm>

DepositBuffer::DepositBuffer(int width, int height)
    : m_width(width)
    , m_height(height)
    , m_values(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f)
{
}

void DepositBuffer::clear()
{
    std::fill(m_values.begin(), m_values.end(), 0.0f);
    m_dirty = bounds();
}

void DepositBuffer::markDirty(const QRect &rect)
{
    m_dirty |= rect & bounds();
}

QRect DepositBuffer::takeDirty()
{
    const QRect dirty = m_dirty;
    m_dirty = QRect();
    return dirty;
}
