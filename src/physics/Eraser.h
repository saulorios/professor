#pragma once

#include "ChalkSample.h"
#include "PhysicsParams.h"

#include <QPointF>

#include <cstdint>
#include <vector>

class DepositBuffer;

// Apagador: a cada passada remove boa parte do pó e espalha levemente o
// restante para os vizinhos, deixando o "fantasma" típico de lousa usada.
// Cada pixel é apagado no máximo uma vez por passada; uma nova passada começa
// a cada toque e sempre que o apagador inverte o sentido (vai e volta).
class Eraser
{
public:
    Eraser(const PhysicsParams &params, DepositBuffer &deposit);

    void begin(const ChalkSample &sample);
    void add(const ChalkSample &sample);
    void end();

private:
    void startPass();
    void apply(float cx, float cy);
    void spreadTo(int x, int y, float amount);

    const PhysicsParams &m_params; // lido a cada uso: ajustes valem na hora
    DepositBuffer &m_deposit;
    std::vector<std::uint32_t> m_passOf; // última passada que apagou cada pixel
    std::uint32_t m_pass = 0;

    QPointF m_last;
    double m_carry = 0.0;       // distância percorrida desde a última aplicação
    double m_moveX = 0.0;       // deslocamento acumulado desde o último sentido medido
    double m_moveY = 0.0;
    double m_dirX = 0.0;        // sentido atual do movimento (vetor unitário)
    double m_dirY = 0.0;
    bool m_hasDirection = false;
    bool m_active = false;
};
