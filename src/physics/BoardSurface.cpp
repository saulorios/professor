#include "BoardSurface.h"
#include "Noise.h"

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
    : m_width(params.boardWidth)
    , m_height(params.boardHeight)
    , m_heights(static_cast<std::size_t>(params.boardWidth) * static_cast<std::size_t>(params.boardHeight))
{
    // 2 oitavas (grão fino + ondulação leve), combinadas por peso em 0..1
    const float totalWeight = params.surfaceFineWeight + params.surfaceWaveWeight;
    const std::uint32_t waveSeed = params.surfaceSeed + 1; // oitava independente da primeira

    std::size_t i = 0;
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x, ++i) {
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            const float fine = valueNoise(px, py, params.surfaceFineScale, params.surfaceSeed);
            const float wave = valueNoise(px, py, params.surfaceWaveScale, waveSeed);
            const float combined = (fine * params.surfaceFineWeight + wave * params.surfaceWaveWeight) / totalWeight;
            // Curva de contraste: com gamma > 1 há mais vales que picos, e o giz pega com menos pressão
            m_heights[i] = std::pow(combined, params.surfaceGamma);
        }
    }
}
