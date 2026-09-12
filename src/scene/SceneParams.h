#pragma once

#include <QString>

// Constantes ajustáveis da cena. Tudo em unidades da lousa (graus nos ângulos),
// exceto onde indicado "unidades da fonte".
struct SceneParams {
    double boardWidth = 160.0;
    double boardHeight = 90.0;
    double margin = 4.0;            // distância da área útil até as bordas (e até a faixa da legenda)

    // --- Layout ---
    double captionBandHeight = 5.0; // faixa reservada à legenda da fala, na base da lousa
    double relativeMargin = 3.0;    // "margem" padrão de abaixo_de/acima_de/direita_de/esquerda_de
    double collisionGap = 1.0;      // folga ao afastar um elemento de outro
    int maxCollisionAttempts = 10;  // depois disso a sobreposição é aceita (com aviso)
    double highlightGap = 1.0;      // folga entre o elemento e o sublinhado, círculo ou caixa

    double curveTolerance = 0.03;   // desvio máximo entre a curva e a corda (amostragem adaptativa)
    int minCurveSegments = 12;      // mínimo de segmentos num círculo completo

    double dashLength = 1.8;        // "tracejado"
    double dashGap = 1.1;
    double dotLength = 0.35;        // "pontilhado"
    double dotGap = 1.0;

    double arrowHeadLength = 2.5;
    double arrowHeadAngle = 26.0;   // abertura de cada lado da ponta da seta
    double connectGap = 0.8;        // folga entre a linha e a borda dos elementos ligados

    // --- Texto (comando "escrever") ---
    QString fontPath = ":/fonts/rowmans.jhf"; // "fonte": "normal" — Hershey Roman Simplex (traço único)
    QString cursiveFontPath = ":/fonts/scripts.jhf"; // "fonte": "cursiva" — Hershey Script Simplex
    double textDefaultSize = 4.0;   // "tamanho" padrão: altura das maiúsculas
    double scriptScale = 0.6;       // índices e expoentes: 60% do tamanho
    double superscriptRise = 0.45;  // o expoente sobe esta fração do tamanho
    double subscriptDrop = 0.25;    // o índice desce esta fração do tamanho
    double kerning = 2.0;           // aproximação dos pares da tabela (unidades da fonte)
    double tracking = -2.0;         // ajuste do espaço entre letras (unidades da fonte; < 0 aproxima)
    double cursiveTracking = 0.0;   // na cursiva as letras se emendam nos próprios limites

    // --- Objetos 3D (comando "objeto_3d") ---
    double cavalierAngle = 45.0;      // "cavaleira": ângulo do eixo da profundidade
    double cavalierReduction = 0.5;   // "cavaleira": redução da profundidade
    double isometricAngle = 30.0;     // "isometrica": inclinação dos eixos
    double twoPointRotation = 30.0;   // "perspectiva_2": "rotacao" padrão
    double fieldOfView = 35.0;        // abertura da câmera pinhole (evita distorção exagerada)
    double eyeLow = -0.25;            // "altura_olho": fração da altura do objeto onde fica o olho
    double eyeMedium = 0.5;
    double eyeHigh = 1.5;
    int curveSegments = 48;           // facetas da malha dos sólidos curvos (só para oclusão)
    int curveSamples = 96;            // pontos das elipses e silhuetas desenhadas
    int sphereRings = 16;             // paralelos da malha da esfera
    double hiddenSampleSpacing = 0.25;// passo da amostragem que separa visível de oculto
    double labelSize = 3.0;           // "rotular": tamanho do texto
    double labelGap = 1.0;            // "rotular": afastamento do vértice
    double dimensionOffset = 3.0;     // "cotar": distância da linha de cota até a aresta
    double dimensionTick = 1.2;       // "cotar": traço nas extremidades da cota
    double dimensionTextSize = 3.0;
    double dimensionTextGap = 0.8;    // folga entre a linha de cota e o texto
    double vanishingLimit = 3000.0;   // pontos de fuga além disso são descartados (retas paralelas)
};
