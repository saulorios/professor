#pragma once

#include <cstdint>

// Ruído determinístico baseado em hash da posição (sem rand()).
namespace noise {

// Hash inteiro de uma posição 2D
inline std::uint32_t hash(std::int32_t x, std::int32_t y, std::uint32_t seed)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                    + static_cast<std::uint32_t>(y) * 668265263u
                    + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Valor em [0, 1] para uma posição inteira
inline float value(std::int32_t x, std::int32_t y, std::uint32_t seed)
{
    return static_cast<float>(hash(x, y, seed) & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

} // namespace noise
