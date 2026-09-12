#pragma once

#include <cstdint>

// Todas as constantes ajustáveis da física do giz.
// Distâncias em pixels da lousa, tempos em milissegundos, velocidades em px/ms.
struct PhysicsParams {
    // --- Lousa ---
    // O canvas tem largura fixa e cresce para baixo em blocos de uma tela.
    // boardHeight é a altura da TELA (viewport), não a do canvas.
    int boardWidth = 1920;               // 160 unidades × 12 px
    int boardHeight = 1080;              // 90 unidades × 12 px (uma tela)

    // --- Superfície (height map) ---
    std::uint32_t surfaceSeed = 1337;
    float surfaceFineScale = 2.0f;       // tamanho do grão fino (px)
    float surfaceWaveScale = 40.0f;      // tamanho da ondulação leve (px)
    float surfaceFineWeight = 0.7f;      // peso do grão fino na altura final
    float surfaceWaveWeight = 0.3f;      // peso da ondulação na altura final
    float surfaceGamma = 1.8f;           // altura = combinado^gamma (> 1: mais vales que picos)

    // --- Giz ---
    float tipRadius = 3.0f;              // raio da ponta de um giz novo (px)
    float maxTipRadius = 4.5f;           // limite do raio com o desgaste (px)
    float wearRate = 2.5e-6f;            // crescimento do raio por unidade de giz depositada
    float efficiency = 0.25f;            // eficiência de depósito
    float tiltStretch = 2.0f;            // alongamento extra da ponta com tilt = 1

    // --- Traço ---
    float kVelocity = 0.8f;              // fatorTempo = 1 / (1 + velocidade * kVelocity)
    float substepSpacing = 0.5f;         // distância entre sub-passos (px, > 0)
    float velocitySmoothing = 0.35f;     // peso da nova medida na média móvel da velocidade
    double minSampleIntervalMs = 1.0;    // evita divisão por intervalos nulos entre amostras
    float noiseBase = 0.7f;              // qtd *= noiseBase + noiseAmount * ruido(x,y)
    float noiseAmount = 0.3f;
    std::uint32_t noiseSeed = 7331;

    // --- Apagador ---
    float eraserRadius = 20.0f;          // (px)
    float eraserStrength = 0.85f;        // fração do depósito removida por passada
    float eraserSpread = 0.25f;          // fração do que resta espalhada para os vizinhos
    float eraserNoise = 0.3f;            // irregularidade da remoção (textura do fantasma)
    float eraserSpacing = 2.0f;          // distância entre aplicações do apagador (px, > 0)
    float eraserReversalDistance = 10.0f;// deslocamento mínimo para medir o sentido do movimento
    float eraserReversalCos = -0.3f;     // abaixo deste cosseno entre sentidos: nova passada
};
