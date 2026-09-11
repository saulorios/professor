#pragma once

#include <QRect>

#include <cstddef>
#include <vector>

// Quantidade de pó de giz em cada pixel da lousa (0..1), com controle da
// região alterada desde a última leitura (dirty rect).
class DepositBuffer
{
public:
    DepositBuffer(int width, int height);

    int width() const { return m_width; }
    int height() const { return m_height; }
    QRect bounds() const { return QRect(0, 0, m_width, m_height); }

    float *data() { return m_values.data(); }
    const float *data() const { return m_values.data(); }
    float at(int x, int y) const { return m_values[index(x, y)]; }

    std::size_t index(int x, int y) const
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x);
    }

    // Zera todo o depósito e marca a lousa inteira como alterada
    void clear();

    // Acumula uma região alterada (recortada aos limites da lousa)
    void markDirty(const QRect &rect);

    // Devolve a região alterada acumulada e a zera
    QRect takeDirty();

private:
    int m_width;
    int m_height;
    std::vector<float> m_values;
    QRect m_dirty;
};
