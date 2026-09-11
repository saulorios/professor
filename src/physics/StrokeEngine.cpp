#include "StrokeEngine.h"
#include "BoardSurface.h"
#include "ChalkStick.h"
#include "DepositBuffer.h"
#include "Noise.h"

#include <algorithm>
#include <cmath>

StrokeEngine::StrokeEngine(const PhysicsParams &params, const BoardSurface &surface,
                           DepositBuffer &deposit, ChalkStick &chalk)
    : m_params(params)
    , m_surface(surface)
    , m_deposit(deposit)
    , m_chalk(chalk)
{
}

void StrokeEngine::begin(const ChalkSample &sample)
{
    m_active = true;
    m_last = sample;
    m_velocity = 0.0f;
    m_hasVelocity = false;
    m_carry = 0.0;

    // O toque inicial do giz já deixa marca
    stamp(static_cast<float>(sample.pos.x()), static_cast<float>(sample.pos.y()),
          sample.pressure, sample.tilt);
}

void StrokeEngine::add(const ChalkSample &sample)
{
    if (!m_active) {
        begin(sample);
        return;
    }

    const double dx = sample.pos.x() - m_last.pos.x();
    const double dy = sample.pos.y() - m_last.pos.y();
    const double length = std::hypot(dx, dy);

    // Velocidade (px/ms) suavizada por média móvel exponencial
    const double dt = std::max(sample.timeMs - m_last.timeMs, m_params.minSampleIntervalMs);
    const auto measured = static_cast<float>(length / dt);
    if (m_hasVelocity)
        m_velocity += m_params.velocitySmoothing * (measured - m_velocity);
    else
        m_velocity = measured;
    m_hasVelocity = true;

    if (length > 0.0) {
        m_dirX = static_cast<float>(dx / length);
        m_dirY = static_cast<float>(dy / length);
    }

    // Sub-passos a cada substepSpacing px, continuando de onde o segmento anterior parou
    const double spacing = m_params.substepSpacing;
    double t = spacing - m_carry;
    for (; t <= length; t += spacing) {
        const double f = t / length;
        const auto ff = static_cast<float>(f);
        stamp(static_cast<float>(m_last.pos.x() + dx * f),
              static_cast<float>(m_last.pos.y() + dy * f),
              m_last.pressure + (sample.pressure - m_last.pressure) * ff,
              m_last.tilt + (sample.tilt - m_last.tilt) * ff);
    }
    m_carry = length - (t - spacing);
    m_last = sample;
}

void StrokeEngine::end()
{
    m_active = false;
}

void StrokeEngine::stamp(float cx, float cy, float pressure, float tilt)
{
    const ChalkStick::Tip tip = m_chalk.tip(tilt);
    const float reach = std::max(tip.along, tip.across);

    // Área da ponta recortada aos limites da lousa
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - reach)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - reach)));
    const int x1 = std::min(m_deposit.width() - 1, static_cast<int>(std::ceil(cx + reach)));
    const int y1 = std::min(m_deposit.height() - 1, static_cast<int>(std::ceil(cy + reach)));
    if (x0 > x1 || y0 > y1)
        return;

    const float timeFactor = 1.0f / (1.0f + m_velocity * m_params.kVelocity);
    const float efficiency = m_chalk.efficiency();
    const float invAlong2 = 1.0f / (tip.along * tip.along);
    const float invAcross2 = 1.0f / (tip.across * tip.across);
    float *deposit = m_deposit.data();

    for (int y = y0; y <= y1; ++y) {
        const float dy = y + 0.5f - cy;
        for (int x = x0; x <= x1; ++x) {
            const float dx = x + 0.5f - cx;

            // Coordenadas no referencial do traço: u ao longo, v através
            const float u = dx * m_dirX + dy * m_dirY;
            const float v = dy * m_dirX - dx * m_dirY;
            if (u * u * invAlong2 + v * v * invAcross2 > 1.0f)
                continue;

            const float contact = pressure - m_surface.heightAt(x, y);
            if (contact <= 0.0f)
                continue;

            const float grain = m_params.noiseBase
                              + m_params.noiseAmount * noise::value(x, y, m_params.noiseSeed);
            const float amount = contact * timeFactor * efficiency * grain;

            float &d = deposit[m_deposit.index(x, y)];
            d = std::min(1.0f, d + amount * (1.0f - d));
            m_chalk.wear(amount);
        }
    }

    m_deposit.markDirty(QRect(QPoint(x0, y0), QPoint(x1, y1)));
}
