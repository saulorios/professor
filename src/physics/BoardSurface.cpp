#include "BoardSurface.h"
#include "Noise.h"

#include <algorithm>
#include <cmath>

namespace {

float smoothstep(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

// Value noise: valores pseudoaleatórios numa grade de lado `cell` px,
// interpolados suavemente entre os pontos da grade
float valueNoise(float x, float y, float cell, std::uint32_t seed)
{
    const float gx = x / cell;
    const float gy = y / cell;
    const float fx = std::floor(gx);
    const float fy = std::floor(gy);
    const auto ix = static_cast<std::int32_t>(fx);
    const auto iy = static_cast<std::int32_t>(fy);
    const float tx = smoothstep(gx - fx);
    const float ty = smoothstep(gy - fy);

    const float v00 = noise::value(ix, iy, seed);
    const float v10 = noise::value(ix + 1, iy, seed);
    const float v01 = noise::value(ix, iy + 1, seed);
    const float v11 = noise::value(ix + 1, iy + 1, seed);

    const float top = v00 + (v10 - v00) * tx;
    const float bottom = v01 + (v11 - v01) * tx;
    return top + (bottom - top) * ty;
}

} // namespace

BoardSurface::BoardSurface(const PhysicsParams &params)
    : m_params(params)
    , m_width(params.boardWidth)
    , m_blockHeight(std::max(1, params.boardHeight))
    , m_height(m_blockHeight)
    , m_heights(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height))
{
    fill(0, m_height);
}

void BoardSurface::generate(const PhysicsParams &params)
{
    m_params = params;
    fill(0, m_height);
}

void BoardSurface::ensureHeight(int height)
{
    if (height <= m_height)
        return;
    const int blocks = (height + m_blockHeight - 1) / m_blockHeight;
    const int from = m_height;
    m_height = blocks * m_blockHeight;
    // reserve antes do resize: sem folga dobrada de alocação
    const std::size_t cells = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    m_heights.reserve(cells);
    m_heights.resize(cells);
    fill(from, m_height);
}

void BoardSurface::reset()
{
    if (m_height == m_blockHeight)
        return;
    m_height = m_blockHeight;
    m_heights.resize(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height));
    m_heights.shrink_to_fit();
}

void BoardSurface::fill(int from, int to)
{
    // 2 oitavas (grão fino + ondulação leve), combinadas por peso em 0..1
    const float totalWeight = m_params.surfaceFineWeight + m_params.surfaceWaveWeight;
    const std::uint32_t waveSeed = m_params.surfaceSeed + 1; // oitava independente da primeira

    std::size_t i = static_cast<std::size_t>(from) * static_cast<std::size_t>(m_width);
    for (int y = from; y < to; ++y) {
        for (int x = 0; x < m_width; ++x, ++i) {
            // Coordenada absoluta do canvas: o grão não "pula" ao rolar
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            const float fine = valueNoise(px, py, m_params.surfaceFineScale, m_params.surfaceSeed);
            const float wave = valueNoise(px, py, m_params.surfaceWaveScale, waveSeed);
            const float combined = (fine * m_params.surfaceFineWeight + wave * m_params.surfaceWaveWeight) / totalWeight;
            // Curva de contraste: com gamma > 1 há mais vales que picos, e o giz pega com menos pressão
            m_heights[i] = std::pow(combined, m_params.surfaceGamma);
        }
    }
}
