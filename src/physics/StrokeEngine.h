#pragma once

#include "ChalkSample.h"
#include "PhysicsParams.h"

class BoardSurface;
class ChalkStick;
class DepositBuffer;

// Transforma a sequência de ChalkSamples de um traço em depósito de giz:
// calcula a velocidade, interpola sub-passos entre as amostras (posição,
// pressão e tilt) e aplica o depósito em cada pixel sob a ponta do giz.
class StrokeEngine
{
public:
    StrokeEngine(const PhysicsParams &params, const BoardSurface &surface,
                 DepositBuffer &deposit, ChalkStick &chalk);

    void begin(const ChalkSample &sample);
    void add(const ChalkSample &sample);
    void end();

private:
    // Depósito de um sub-passo, com a ponta centrada em (cx, cy)
    void stamp(float cx, float cy, float pressure, float tilt);

    PhysicsParams m_params;
    const BoardSurface &m_surface;
    DepositBuffer &m_deposit;
    ChalkStick &m_chalk;

    ChalkSample m_last{};
    float m_velocity = 0.0f;     // px/ms, suavizada
    bool m_hasVelocity = false;
    float m_dirX = 1.0f;         // direção atual do traço (vetor unitário)
    float m_dirY = 0.0f;
    double m_carry = 0.0;        // distância percorrida desde o último sub-passo
    bool m_active = false;
};
