#pragma once

#include "HersheyFont.h"
#include "SceneParams.h"
#include "hand/Polyline.h"

#include <QPointF>
#include <QString>

#include <cstddef>
#include <vector>

// Onde cada letra ficou dentro do texto, para o Humanizer poder variar
// letra por letra (e não o texto inteiro de uma vez)
struct GlyphRun {
    QChar character;
    QChar previous;        // caractere anterior no texto (espaço, vírgula, ponto...)
    int index = 0;         // posição do glifo na frase
    std::size_t first = 0; // primeiro traço deste glifo
    std::size_t count = 0; // quantos traços
    QPointF origin;        // caneta e linha de base do glifo
    double scale = 1.0;    // unidades da lousa por unidade da fonte
};

// Posiciona o texto em polilinhas (unidades da lousa), letra por letra, com
// kerning simples. "tamanho" é a altura das maiúsculas. Índices e expoentes:
// "_" e "^" valem para o próximo caractere ou para o grupo entre {} — H_2O,
// x^2, e^{-x} — com 60% do tamanho e deslocamento vertical.
class TextLayout
{
public:
    TextLayout(const HersheyFont &font, const SceneParams &params);

    // Linha de base em y = 0, começando em x = 0. Caracteres sem glifo são
    // pulados e acumulados em `missing`; `runs` recebe a posição de cada letra.
    std::vector<Polyline> layout(const QString &text, double size, QString *missing = nullptr,
                                 std::vector<GlyphRun> *runs = nullptr) const;

private:
    enum class Script { Normal, Subscript, Superscript };

    // Estado que caminha junto com a escrita
    struct Cursor {
        double pen = 0.0;
        QChar previous;     // último glifo desenhado (para o kerning)
        QChar rawPrevious;  // último caractere do texto, glifo ou não
        int index = 0;      // contador de glifos
    };

    void placeRun(const QString &run, Script script, double size, Cursor &cursor,
                  std::vector<Polyline> &out, QString *missing, std::vector<GlyphRun> *runs) const;
    double kerning(QChar left, QChar right) const;

    const HersheyFont &m_font;
    SceneParams m_params;
};
