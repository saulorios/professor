#include "DepositBuffer.h"

#include <algorithm>

DepositBuffer::DepositBuffer(int width, int blockHeight)
    : m_width(width)
    , m_blockHeight(std::max(1, blockHeight))
    , m_height(m_blockHeight)
    , m_values(static_cast<std::size_t>(width) * static_cast<std::size_t>(m_blockHeight), 0.0f)
{
}

bool DepositBuffer::ensureHeight(int height)
{
    if (height <= m_height)
        return false;
    // Cresce em blocos inteiros de uma tela
    const int blocks = (height + m_blockHeight - 1) / m_blockHeight;
    m_height = blocks * m_blockHeight;
    // reserve antes do resize: o vetor cresce exatamente o pedido, sem folga dobrada
    const std::size_t cells = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    m_values.reserve(cells);
    m_values.resize(cells, 0.0f);
    return true;
}

void DepositBuffer::clear()
{
    std::fill(m_values.begin(), m_values.end(), 0.0f);
    m_dirty = bounds();
}

void DepositBuffer::reset()
{
    m_height = m_blockHeight;
    m_values.assign(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height), 0.0f);
    m_values.shrink_to_fit();
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
