#pragma once

#include "BoardSurface.h"
#include "ChalkStick.h"
#include "DepositBuffer.h"
#include "PhysicsParams.h"

// Estado físico compartilhado da lousa: superfície, depósito e giz.
// Mouse, mesa digitalizadora e mão virtual escrevem no mesmo Board,
// cada um com o seu próprio StrokeEngine / Eraser.
class Board
{
public:
    explicit Board(const PhysicsParams &p = PhysicsParams())
        : params(p)
        , surface(params)
        , deposit(params.boardWidth, params.boardHeight)
        , chalk(params)
    {
    }

    Board(const Board &) = delete;
    Board &operator=(const Board &) = delete;

    // Zera todo o depósito e troca o giz por um novo
    void clear()
    {
        deposit.clear();
        chalk.reset();
    }

    const PhysicsParams params;
    BoardSurface surface;
    DepositBuffer deposit;
    ChalkStick chalk;
};
