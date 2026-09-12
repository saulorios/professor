#pragma once

#include "BoardSurface.h"
#include "ChalkStick.h"
#include "DepositBuffer.h"
#include "PhysicsParams.h"

// Estado físico compartilhado da lousa: parâmetros, superfície, depósito e giz.
// Mouse, mesa digitalizadora e mão virtual escrevem no mesmo Board, cada um com
// o seu próprio StrokeEngine / Eraser, que leem os parâmetros daqui: ajustes
// feitos com setParams() valem para os próximos traços.
class Board
{
    PhysicsParams m_params; // declarado primeiro: a superfície é gerada a partir dele

public:
    explicit Board(const PhysicsParams &p = PhysicsParams())
        : m_params(p)
        , surface(m_params)
        , deposit(m_params.boardWidth, m_params.boardHeight)
        , chalk(m_params)
    {
    }

    Board(const Board &) = delete;
    Board &operator=(const Board &) = delete;

    const PhysicsParams &params() const { return m_params; }

    // Altura do canvas em pixels (múltiplo da altura de uma tela)
    int canvasHeight() const { return deposit.height(); }
    int screenHeight() const { return m_params.boardHeight; }

    // Faz o canvas crescer até caber esta altura; devolve true se cresceu.
    // Chame só entre traços: o crescimento realoca os buffers da física.
    bool ensureHeight(int height)
    {
        surface.ensureHeight(height);
        return deposit.ensureHeight(height);
    }

    // Troca os parâmetros; a resolução da lousa é fixa (buffers já alocados).
    // Devolve true se a superfície foi regenerada (a tela precisa recompor o fundo).
    bool setParams(const PhysicsParams &p)
    {
        const bool surfaceChanged = p.surfaceSeed != m_params.surfaceSeed
                                 || p.surfaceFineScale != m_params.surfaceFineScale
                                 || p.surfaceWaveScale != m_params.surfaceWaveScale
                                 || p.surfaceFineWeight != m_params.surfaceFineWeight
                                 || p.surfaceWaveWeight != m_params.surfaceWaveWeight
                                 || p.surfaceGamma != m_params.surfaceGamma;
        const int width = m_params.boardWidth;
        const int height = m_params.boardHeight;
        m_params = p;
        m_params.boardWidth = width;
        m_params.boardHeight = height;

        chalk.setParams(m_params);
        if (surfaceChanged)
            surface.generate(m_params);
        return surfaceChanged;
    }

    // Zera todo o depósito e troca o giz por um novo (mantém o tamanho do canvas)
    void clear()
    {
        deposit.clear();
        chalk.reset();
    }

    // Volta ao canvas de uma tela só, vazio
    void reset()
    {
        deposit.reset();
        surface.reset();
        chalk.reset();
    }

    BoardSurface surface;
    DepositBuffer deposit;
    ChalkStick chalk;
};
