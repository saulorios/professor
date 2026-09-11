#pragma once

#include "PhysicsParams.h"

#include <cstddef>
#include <vector>

// Relevo da lousa: height map 0..1 por pixel, gerado com value noise em
// 2 oitavas (grão fino + ondulação leve) e seed fixa. Quanto maior a altura,
// mais pressão o giz precisa para marcar aquele pixel.
class BoardSurface
{
public:
    explicit BoardSurface(const PhysicsParams &params);

    // Gera o height map de novo com os parâmetros de superfície (mesma resolução)
    void generate(const PhysicsParams &params);

    int width() const { return m_width; }
    int height() const { return m_height; }

    float heightAt(int x, int y) const
    {
        return m_heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width)
                         + static_cast<std::size_t>(x)];
    }

private:
    int m_width;
    int m_height;
    std::vector<float> m_heights;
};
