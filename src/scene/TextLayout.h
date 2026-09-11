#pragma once

#include "HersheyFont.h"
#include "SceneParams.h"
#include "hand/Polyline.h"

#include <QString>

#include <vector>

// Posiciona o texto em polilinhas (unidades da lousa), letra por letra, com
// kerning simples. "tamanho" é a altura das maiúsculas. Índices e expoentes:
// "_" e "^" valem para o próximo caractere ou para o grupo entre {} — H_2O,
// x^2, e^{-x} — com 60% do tamanho e deslocamento vertical.
class TextLayout
{
public:
    TextLayout(const HersheyFont &font, const SceneParams &params);

    // Linha de base em y = 0, começando em x = 0. Caracteres sem glifo são
    // pulados e acumulados em `missing`.
    std::vector<Polyline> layout(const QString &text, double size, QString *missing = nullptr) const;

private:
    enum class Script { Normal, Subscript, Superscript };

    void placeRun(const QString &run, Script script, double size, double &pen, QChar &previous,
                  std::vector<Polyline> &out, QString *missing) const;
    double kerning(QChar left, QChar right) const;

    const HersheyFont &m_font;
    SceneParams m_params;
};
