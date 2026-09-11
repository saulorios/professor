#pragma once

#include <cstdint>

// Constantes ajustáveis da mão virtual.
// Distâncias em unidades da lousa, velocidades em unidades/s, tempos em ms.
struct HandParams {
    // --- Conversão (fronteira hand/ → physics/) ---
    double pixelsPerUnit = 12.0;         // 1920 px / 160 unidades

    // --- Perfil de velocidade ---
    double baseSpeed = 30.0;             // velocidade de cruzeiro do giz
    double acceleration = 220.0;         // aceleração/desaceleração máxima (unidades/s²)
    double startSpeed = 2.0;             // velocidade no toque inicial
    double endSpeed = 3.0;               // velocidade ao levantar o giz
    double curveSlowdown = 0.6;          // quanto a curvatura freia a mão
    double minCurveSpeed = 4.0;          // velocidade mínima nas quinas
    double sampleSpacing = 0.25;         // distância entre amostras ao longo do traço

    // --- Pressão ---
    float pressureLight = 0.50f;         // "pressao": "leve"
    float pressureNormal = 0.68f;        // "pressao": "normal"
    float pressureStrong = 0.85f;        // "pressao": "forte"
    float pressureStart = 0.60f;         // fração da pressão base no toque inicial
    float pressureEnd = 0.75f;           // fração da pressão base ao levantar
    double pressureRampIn = 1.5;         // distância até atingir a pressão plena
    double pressureRampOut = 2.0;        // distância final em que a pressão alivia
    float pressureVariation = 0.06f;     // variação lenta (±) ao longo do traço
    double pressureWavelength = 13.0;    // comprimento de onda dessa variação
    float tilt = 0.0f;                   // giz de ponta

    // --- Tremor (wobble) determinístico de baixa frequência ---
    double wobbleMin = 0.1;              // amplitude mínima (sorteada por traço)
    double wobbleMax = 0.3;              // amplitude máxima
    double wobbleWavelength = 9.0;       // comprimento de onda principal
    std::uint32_t wobbleSeed = 2024;

    // --- Entre traços ---
    double closedOvershoot = 0.4;        // traços fechados passam um pouco do ponto inicial
    double penLiftMs = 90.0;             // tempo para levantar e apoiar o giz
    double travelSpeed = 150.0;          // deslocamento no ar até o próximo traço

    // --- Apagador ---
    double eraserSpeed = 70.0;
    double eraserRowSpacing = 2.5;       // distância entre as linhas do zigue-zague
    double eraserMargin = 1.0;           // margem além da bounding box

    // --- Relógio ---
    int tickIntervalMs = 16;             // QTimer de ~60 Hz
};
