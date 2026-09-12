#pragma once

#include <QPointF>

// Onde está o giz da mão virtual, para a tela desenhá-lo por cima da lousa.
// Nada disso entra no DepositBuffer: é só aparência, sem rastro permanente.
struct ChalkPose {
    QPointF tip;          // ponta do giz, em pixels da lousa (é a amostra atual)
    double heading = 0.0; // direção do traço, em radianos (Y para baixo)
    double lift = 0.0;    // 0 = encostado na lousa, 1 = no alto, entre um traço e outro
    bool drawing = false; // false quando a mão está no ar
};
