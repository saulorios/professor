#pragma once

#include "TextLayout.h"
#include "hand/VirtualHand.h"

#include <QString>

#include <cstdint>
#include <vector>

// Constantes da humanização da escrita. "intensity" é o mestre: 0 devolve o
// texto exatamente como saiu da fonte; os demais valores são escalados por ela.
struct HumanizerParams {
    double intensity = 0.5;          // 0..1
    double scaleJitter = 0.04;       // ±4% na altura e na largura de cada letra
    double rotationDegrees = 2.5;    // ±2,5° em torno da base da letra
    double baselineJitter = 0.15;    // ±0,15 u, ondulado ao longo da palavra
    double baselineWavelength = 3.5; // em letras: o quanto a ondulação é lenta
    double bowing = 0.015;           // flecha de até 1,5% do comprimento do segmento
    double minSegment = 0.6;         // só encurva segmentos retos maiores que isto (u)
    int bowSegments = 6;             // em quantos pedaços a curva suave é feita
    double cornerRadius = 0.16;      // arredondamento das quinas dentro de um traço (u)
    double closeGap = 0.02;          // falha ou sobreposição no fecho (fração do perímetro)
    double overshoot = 0.02;         // quanto um traço passa do ponto de encontro
    double speedJitter = 0.10;       // ±10% de velocidade entre letras
    double quickChance = 0.15;       // fração de traços mais rápidos e leves
    double quickSpeed = 1.25;
    double quickPressure = 0.85;
    double pauseWord = 70.0;         // micro-pausa entre palavras (ms)
    double pauseComma = 130.0;       // depois de vírgula, ponto e vírgula, dois pontos
    double pauseStop = 220.0;        // depois de ponto, exclamação, interrogação
    double cursiveScale = 0.5;       // a cursiva usa metade da intensidade
    std::uint32_t seed = 7;          // seed da aula
};

// Tira o ar de impresso da escrita: cada ocorrência de cada caractere recebe
// uma seed própria (caractere + posição na frase + seed da aula), então o mesmo
// "a" escrito duas vezes sai diferente. Trabalha nas polilinhas do glifo, antes
// de entregar à mão, e devolve também o ritmo de cada traço.
class Humanizer
{
public:
    explicit Humanizer(const HumanizerParams &params = HumanizerParams());

    void setParams(const HumanizerParams &params) { m_params = params; }
    const HumanizerParams &params() const { return m_params; }

    // `cursive` reduz a intensidade pela metade (a cursiva já é irregular)
    std::vector<HandStroke> apply(const std::vector<Polyline> &strokes, const std::vector<GlyphRun> &runs,
                                  PressureLevel pressure, bool cursive) const;

private:
    HumanizerParams m_params;
};
