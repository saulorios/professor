#include "Eraser.h"
#include "DepositBuffer.h"
#include "Noise.h"

#include <algorithm>
#include <cmath>

Eraser::Eraser(const PhysicsParams &params, DepositBuffer &deposit)
    : m_params(params)
    , m_deposit(deposit)
    , m_passOf(static_cast<std::size_t>(deposit.width()) * static_cast<std::size_t>(deposit.height()), 0u)
{
}

void Eraser::begin(const ChalkSample &sample)
{
    m_active = true;
    m_last = sample.pos;
    m_carry = 0.0;
    m_moveX = 0.0;
    m_moveY = 0.0;
    m_hasDirection = false;

    startPass();
    apply(static_cast<float>(sample.pos.x()), static_cast<float>(sample.pos.y()));
}

void Eraser::add(const ChalkSample &sample)
{
    if (!m_active) {
        begin(sample);
        return;
    }

    const double dx = sample.pos.x() - m_last.x();
    const double dy = sample.pos.y() - m_last.y();
    const double length = std::hypot(dx, dy);

    // Inversão de sentido (vai e volta) inicia uma nova passada
    m_moveX += dx;
    m_moveY += dy;
    const double moved = std::hypot(m_moveX, m_moveY);
    if (moved >= m_params.eraserReversalDistance) {
        const double dirX = m_moveX / moved;
        const double dirY = m_moveY / moved;
        if (m_hasDirection && dirX * m_dirX + dirY * m_dirY < m_params.eraserReversalCos)
            startPass();
        m_dirX = dirX;
        m_dirY = dirY;
        m_hasDirection = true;
        m_moveX = 0.0;
        m_moveY = 0.0;
    }

    // Aplicações a cada eraserSpacing px ao longo do movimento
    const double spacing = m_params.eraserSpacing;
    double t = spacing - m_carry;
    for (; t <= length; t += spacing) {
        const double f = t / length;
        apply(static_cast<float>(m_last.x() + dx * f), static_cast<float>(m_last.y() + dy * f));
    }
    m_carry = length - (t - spacing);
    m_last = sample.pos;
}

void Eraser::end()
{
    m_active = false;
}

void Eraser::startPass()
{
    ++m_pass;
    if (m_pass == 0) { // o contador deu a volta: reinicia as marcações
        std::fill(m_passOf.begin(), m_passOf.end(), 0u);
        m_pass = 1;
    }
}

void Eraser::apply(float cx, float cy)
{
    const float radius = m_params.eraserRadius;

    // Área do apagador recortada aos limites da lousa
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int x1 = std::min(m_deposit.width() - 1, static_cast<int>(std::ceil(cx + radius)));
    const int y1 = std::min(m_deposit.height() - 1, static_cast<int>(std::ceil(cy + radius)));
    if (x0 > x1 || y0 > y1)
        return;

    // O canvas pode ter crescido desde a última passada
    const std::size_t cells = static_cast<std::size_t>(m_deposit.width())
                            * static_cast<std::size_t>(m_deposit.height());
    if (m_passOf.size() < cells)
        m_passOf.resize(cells, 0u);

    const float radius2 = radius * radius;
    const float keep = 1.0f - m_params.eraserStrength;
    const std::uint32_t passSeed = m_params.noiseSeed + m_pass;
    float *deposit = m_deposit.data();

    for (int y = y0; y <= y1; ++y) {
        const float dy = y + 0.5f - cy;
        for (int x = x0; x <= x1; ++x) {
            const float dx = x + 0.5f - cx;
            if (dx * dx + dy * dy > radius2)
                continue;

            const std::size_t i = m_deposit.index(x, y);
            if (m_passOf[i] == m_pass)
                continue; // já apagado nesta passada
            m_passOf[i] = m_pass;

            float &d = deposit[i];
            if (d <= 0.0f)
                continue;

            // Remoção irregular (varia por pixel e por passada): fantasma com textura
            const float variation = 1.0f + m_params.eraserNoise * (2.0f * noise::value(x, y, passSeed) - 1.0f);
            const float remaining = std::min(d, d * keep * variation);
            const float spread = remaining * m_params.eraserSpread;
            d = remaining - spread;

            // Espalha parte do que restou para os 4 vizinhos
            const float share = 0.25f * spread;
            spreadTo(x - 1, y, share);
            spreadTo(x + 1, y, share);
            spreadTo(x, y - 1, share);
            spreadTo(x, y + 1, share);
        }
    }

    m_deposit.markDirty(QRect(QPoint(x0 - 1, y0 - 1), QPoint(x1 + 1, y1 + 1)));
}

void Eraser::spreadTo(int x, int y, float amount)
{
    if (x < 0 || y < 0 || x >= m_deposit.width() || y >= m_deposit.height())
        return;
    float &d = m_deposit.data()[m_deposit.index(x, y)];
    d = std::min(1.0f, d + amount);
}
