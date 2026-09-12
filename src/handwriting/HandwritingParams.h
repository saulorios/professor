#pragma once

// Constantes ajustáveis do módulo de escrita manual
struct HandwritingParams {
    // --- Formato do banco ---
    int schemaVersion = 1;               // versão gravada em cada arquivo
    int variantDigits = 2;               // A01, A02... (cresce sozinho depois de 99)

    // --- Geometria derivada ---
    double initialDirectionFraction = 0.15; // trecho inicial usado na direção de entrada do stroke

    // --- Gravação em JSON (casas decimais) ---
    int glyphDecimals = 4;               // pontos no espaço do glifo
    int rawDecimals = 2;                 // px do dispositivo
    int timeDecimals = 2;                // ms
    int pressureDecimals = 3;

    // --- HandwritingEngine (sem variação nesta etapa) ---
    double letterSpacing = 0.12;         // entre letras, fração do tamanho
    double wordSpacing = 0.55;           // largura do espaço, fração do tamanho
    double glyphPenUpMs = 140.0;         // giz levantado de uma letra para a outra
    double wordPenUpMs = 260.0;          // e de uma palavra para a outra
};
