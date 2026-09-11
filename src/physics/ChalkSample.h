#pragma once

#include <QPointF>

// Entrada única da física: mouse, mesa digitalizadora e mão virtual
// produzem sempre amostras neste formato.
struct ChalkSample {
    QPointF pos;       // em pixels da lousa
    float   pressure;  // 0..1
    float   tilt;      // 0..1 (0 = giz de ponta, 1 = giz deitado)
    double  timeMs;    // timestamp
};
