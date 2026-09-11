#include "ChalkStick.h"

ChalkStick::ChalkStick(const PhysicsParams &params)
    : m_baseRadius(params.tipRadius)
    , m_maxRadius(params.maxTipRadius)
    , m_wearRate(params.wearRate)
    , m_efficiency(params.efficiency)
    , m_tiltStretch(params.tiltStretch)
    , m_radius(params.tipRadius)
{
}

ChalkStick::Tip ChalkStick::tip(float tilt) const
{
    const float t = std::clamp(tilt, 0.0f, 1.0f);
    return {m_radius * (1.0f + m_tiltStretch * t), m_radius};
}

void ChalkStick::reset()
{
    m_radius = m_baseRadius;
}

void ChalkStick::setParams(const PhysicsParams &params)
{
    m_baseRadius = params.tipRadius;
    m_maxRadius = params.maxTipRadius;
    m_wearRate = params.wearRate;
    m_efficiency = params.efficiency;
    m_tiltStretch = params.tiltStretch;
    m_radius = std::clamp(m_radius, m_baseRadius, std::max(m_baseRadius, m_maxRadius));
}
