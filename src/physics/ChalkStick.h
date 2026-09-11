#pragma once

#include "PhysicsParams.h"

#include <algorithm>

// Bastão de giz: ponta circular que se alarga levemente com o desgaste e
// vira uma elipse, alongada na direção do traço, quando o giz é inclinado.
class ChalkStick
{
public:
    // Semi-eixos da ponta (px)
    struct Tip {
        float along;   // na direção do traço
        float across;  // perpendicular ao traço
    };

    explicit ChalkStick(const PhysicsParams &params);

    float radius() const { return m_radius; }
    float efficiency() const { return m_efficiency; }

    // Forma da ponta para uma inclinação 0..1
    Tip tip(float tilt) const;

    // Desgasta o giz proporcionalmente à quantidade depositada
    void wear(float amount) { m_radius = std::min(m_maxRadius, m_radius + amount * m_wearRate); }

    // Volta a ser um giz novo
    void reset();

private:
    float m_baseRadius;
    float m_maxRadius;
    float m_wearRate;
    float m_efficiency;
    float m_tiltStretch;
    float m_radius;
};
