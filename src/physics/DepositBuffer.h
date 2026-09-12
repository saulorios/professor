#pragma once

#include <QRect>

#include <cstddef>
#include <vector>

// Quantidade de pó de giz em cada pixel do canvas (0..1), com controle da
// região alterada desde a última leitura (dirty rect).
//
// O canvas tem largura fixa e cresce para baixo sob demanda, em blocos de uma
// tela (`blockHeight`): nada é reservado antes da hora. A memória é contínua,
// então os laços da física continuam iguais — mas cresce SEMPRE fora de um
// traço em andamento, nunca no meio dele (ponteiros de `data()` invalidariam).
class DepositBuffer
{
public:
    DepositBuffer(int width, int blockHeight);

    int width() const { return m_width; }
    int height() const { return m_height; }
    int blockHeight() const { return m_blockHeight; }
    QRect bounds() const { return QRect(0, 0, m_width, m_height); }
    std::size_t bytes() const { return m_values.capacity() * sizeof(float); }

    // Garante que o canvas tem pelo menos esta altura (arredonda para blocos
    // inteiros); devolve true se cresceu
    bool ensureHeight(int height);

    float *data() { return m_values.data(); }
    const float *data() const { return m_values.data(); }
    float at(int x, int y) const { return m_values[index(x, y)]; }

    std::size_t index(int x, int y) const
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x);
    }

    // Zera todo o depósito e marca o canvas inteiro como alterado
    void clear();

    // Volta ao tamanho de uma tela, zerado
    void reset();

    // Acumula uma região alterada (recortada aos limites do canvas)
    void markDirty(const QRect &rect);

    // Devolve a região alterada acumulada e a zera
    QRect takeDirty();

private:
    int m_width;
    int m_blockHeight;
    int m_height;
    std::vector<float> m_values;
    QRect m_dirty;
};
