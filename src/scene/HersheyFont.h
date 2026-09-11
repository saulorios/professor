#pragma once

#include "hand/Polyline.h"

#include <QHash>
#include <QString>

#include <vector>

// Glifo Hershey em unidades da fonte (Y para baixo)
struct HersheyGlyph {
    double left = 0.0;               // limite esquerdo (define o espaçamento)
    double right = 0.0;              // limite direito
    std::vector<Polyline> strokes;   // na ordem em que a mão deve traçar
};

// Fonte vetorial Hershey de traço único (.jhf, sequência ASCII 32–127).
// Cada caractere vira polilinhas, com a ordem dos traços normalizada para a
// escrita à mão: de cima para baixo e da esquerda para a direita.
// Letras acentuadas do português que a fonte não tem são compostas a partir da
// letra base + diacrítico (o acento é traçado depois da letra, como à mão).
class HersheyFont
{
public:
    bool load(const QString &path, QString *error = nullptr);
    bool isLoaded() const { return !m_glyphs.isEmpty(); }

    // Glifo do caractere, ou nullptr se a fonte não o tem nem sabe compô-lo
    const HersheyGlyph *glyph(QChar c) const;

    double capTop() const { return m_capTop; }       // topo das maiúsculas
    double xHeightTop() const { return m_xTop; }     // topo das minúsculas
    double baseline() const { return m_baseline; }   // linha de base
    double capHeight() const { return m_baseline - m_capTop; }

private:
    static void normalizeStrokeOrder(HersheyGlyph &glyph);
    void addComposites();

    QHash<QChar, HersheyGlyph> m_glyphs;
    double m_capTop = -12.0;
    double m_xTop = -5.0;
    double m_baseline = 9.0;
};
