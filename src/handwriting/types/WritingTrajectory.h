#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstddef>
#include <vector>

// Saída do HandwritingEngine e entrada do futuro HumanMotionEngine: a
// trajetória da ponta do giz no tempo, em unidades da lousa. Não diz nada
// sobre braço, pulso ou mão — só onde o giz esteve, quando, com que velocidade
// e se estava encostado.
namespace handwriting {

enum class PenState { Down, Up };

// Amostra com o giz encostado
struct TrajectoryPoint {
    QPointF pos;              // unidades da lousa (Y para baixo)
    double timeMs = 0.0;      // desde o início da trajetória
    float pressure = -1.0f;   // 0..1, ou -1 se a captura não tinha
    float tilt = -1.0f;       // graus, ou -1
    double velocity = 0.0;    // unidades da lousa / s
    double direction = 0.0;   // radianos, Y para baixo
};

// Trecho contínuo com o giz encostado (Down) ou levantado (Up).
// Down aponta para `points[first .. first + count)`. Up não tem amostras: vai
// de `from` (em `startMs`) a `to` (em `endMs`) e o caminho no ar fica por conta
// do HumanMotionEngine.
struct TrajectorySegment {
    PenState pen = PenState::Down;
    std::size_t first = 0;
    std::size_t count = 0;
    QPointF from;
    QPointF to;
    double startMs = 0.0;
    double endMs = 0.0;
    int glyph = -1;           // índice em `glyphs` (-1 no ar entre glifos)
    int stroke = -1;          // id do stroke dentro da variante (-1 no Up)
};

// Onde cada caractere ficou
struct GlyphSpan {
    QString character;
    QString variantId;
    std::size_t firstSegment = 0;
    std::size_t segmentCount = 0;
    QRectF bounds;            // unidades da lousa
    QPointF origin;           // início do glifo na linha de base
    double advance = 0.0;     // quanto a caneta andou até o próximo glifo
    double startMs = 0.0;
    double endMs = 0.0;
};

struct WritingTrajectory {
    std::vector<TrajectoryPoint> points;
    std::vector<TrajectorySegment> segments;   // em ordem de tempo, Down e Up alternados
    std::vector<GlyphSpan> glyphs;
    QRectF bounds;
    double durationMs = 0.0;
    QString missing;          // caracteres sem variante no banco (ficaram de fora)
};

} // namespace handwriting
