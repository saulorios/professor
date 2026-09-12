#pragma once

#include "PhysicsParams.h"

#include <cstddef>
#include <vector>

// Relevo da lousa: height map 0..1 por pixel, gerado com value noise em
// 2 oitavas (grão fino + ondulação leve) e seed fixa. Quanto maior a altura,
// mais pressão o giz precisa para marcar aquele pixel.
//
// Acompanha o crescimento do canvas: cada bloco novo é gerado a partir da
// coordenada ABSOLUTA do pixel, então o grão da lousa não muda de aparência
// quando se rola nem quando o canvas cresce.
class BoardSurface
{
public:
    explicit BoardSurface(const PhysicsParams &params);

    // Gera o height map de novo com os parâmetros de superfície
    void generate(const PhysicsParams &params);

    // Garante que o relevo cobre esta altura (gera só as linhas novas)
    void ensureHeight(int height);

    // Volta ao tamanho de uma tela (o relevo de cada pixel é sempre o mesmo)
    void reset();

    int width() const { return m_width; }
    int height() const { return m_height; }
    std::size_t bytes() const { return m_heights.capacity() * sizeof(float); }

    float heightAt(int x, int y) const
    {
        return m_heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width)
                         + static_cast<std::size_t>(x)];
    }

private:
    // Preenche as linhas [from, to) a partir da coordenada absoluta
    void fill(int from, int to);

    PhysicsParams m_params;
    int m_width;
    int m_blockHeight;
    int m_height;
    std::vector<float> m_heights;
};
