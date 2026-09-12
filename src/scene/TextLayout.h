#pragma once

#include "HersheyFont.h"
#include "SceneParams.h"
#include "hand/Polyline.h"

#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstddef>
#include <vector>

// Onde cada letra ficou dentro do texto, para o Humanizer poder variar
// letra por letra (e não o texto inteiro de uma vez)
struct GlyphRun {
    QChar character;
    QChar previous;        // caractere anterior no texto (espaço, vírgula, ponto...)
    int index = 0;         // posição do glifo na frase
    int line = 0;          // em qual linha do bloco ele está
    std::size_t first = 0; // primeiro traço deste glifo
    std::size_t count = 0; // quantos traços
    QPointF origin;        // caneta e linha de base do glifo
    double scale = 1.0;    // unidades da lousa por unidade da fonte
};

// Um texto já quebrado em linhas
struct TextBlock {
    std::vector<Polyline> strokes;
    std::vector<GlyphRun> runs;
    std::vector<QRectF> lines;  // caixa de cada linha (o destacar sublinha uma a uma)
    QString missing;            // caracteres sem glifo, ignorados
    bool shrunk = false;        // alguma palavra teve de encolher para caber
};

// Posiciona o texto em polilinhas (unidades da lousa), letra por letra, com
// kerning simples. "tamanho" é a altura das maiúsculas. Índices e expoentes:
// "_" e "^" valem para o próximo caractere ou para o grupo entre {} — H_2O,
// x^2, e^{-x} — com 60% do tamanho e deslocamento vertical.
//
// Nada ultrapassa a largura pedida: o texto quebra em linhas, preferindo o
// espaço entre palavras, depois um operador (que é repetido no início da linha
// seguinte, como se faz em matemática) e, em último caso, o hífen. Índices,
// expoentes e grupos {…} nunca são partidos.
class TextLayout
{
public:
    TextLayout(const HersheyFont &font, const SceneParams &params);

    // Linha de base da primeira linha em y = 0, começando em x = 0.
    // `maxWidth` <= 0 desliga a quebra.
    TextBlock layout(const QString &text, double size, double maxWidth = 0.0) const;

private:
    enum class Script { Normal, Subscript, Superscript };

    // Um caractere com o seu papel (normal, índice ou expoente)
    struct Atom {
        QChar character;
        Script script = Script::Normal;
    };

    // Um caractere-base com os índices e expoentes colados nele: nunca se parte
    struct Unit {
        std::vector<Atom> atoms;
        double width = 0.0;
        bool space = false;
        bool op = false;      // operador onde a fórmula pode quebrar
        bool letter = false;  // pode receber hífen
    };

    // Trecho que vai inteiro numa linha (uma "palavra")
    struct Group {
        std::vector<Unit> units;
        double width = 0.0;
        double scale = 1.0;   // < 1 quando a palavra sozinha não cabia
        bool startsWithOperator = false;
    };

    std::vector<Unit> parse(const QString &text) const;
    std::vector<Group> group(const std::vector<Unit> &units) const;
    double advance(QChar character, Script script, double size) const;
    double kerning(QChar left, QChar right) const;
    // Coloca uma linha inteira e devolve a caixa dela
    QRectF emitLine(const std::vector<Group> &groups, double size, double baseline, int line,
                    int &glyphIndex, QChar &previous, TextBlock *block) const;

    const HersheyFont &m_font;
    SceneParams m_params;
};
