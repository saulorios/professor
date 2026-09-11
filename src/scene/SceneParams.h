#pragma once

// Constantes ajustáveis da cena. Tudo em unidades da lousa (graus nos ângulos).
struct SceneParams {
    double boardWidth = 160.0;
    double boardHeight = 90.0;
    double margin = 4.0;            // distância das âncoras até a borda da lousa

    double curveTolerance = 0.03;   // desvio máximo entre a curva e a corda (amostragem adaptativa)
    int minCurveSegments = 12;      // mínimo de segmentos num círculo completo

    double dashLength = 1.8;        // "tracejado"
    double dashGap = 1.1;
    double dotLength = 0.35;        // "pontilhado"
    double dotGap = 1.0;

    double arrowHeadLength = 2.5;
    double arrowHeadAngle = 26.0;   // abertura de cada lado da ponta da seta
    double connectGap = 0.8;        // folga entre a linha e a borda dos elementos ligados
};
