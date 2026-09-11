#include "TextLayout.h"

#include <utility>

namespace {

// Pares que ficam visualmente afastados demais só com os limites do glifo
const char *const kKernPairs[] = {
    "AV", "AW", "AY", "AT", "VA", "WA", "YA", "TA", "LT", "LV", "LW", "LY",
    "Ta", "Te", "To", "Tr", "Tu", "Ty", "Va", "Ve", "Vo", "Wa", "We", "Wo",
    "Ya", "Ye", "Yo", "P.", "P,", "T.", "T,", "V.", "V,", "W.", "W,", "Y.",
    "Y,", "F.", "F,", "r.", "r,",
};

} // namespace

TextLayout::TextLayout(const HersheyFont &font, const SceneParams &params)
    : m_font(font)
    , m_params(params)
{
}

std::vector<Polyline> TextLayout::layout(const QString &text, double size, QString *missing) const
{
    // Forma composta (NFC): "a" + acento combinante vira "á"
    const QString s = text.normalized(QString::NormalizationForm_C);
    std::vector<Polyline> out;
    double pen = 0.0;
    QChar previous;

    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if ((c == '_' || c == '^') && i + 1 < s.size()) {
            const Script script = c == '_' ? Script::Subscript : Script::Superscript;
            QString run;
            if (s[i + 1] == '{') { // grupo até a chave de fechamento (ou até o fim)
                const qsizetype close = s.indexOf('}', i + 2);
                const qsizetype end = close < 0 ? s.size() : close;
                run = s.mid(i + 2, end - (i + 2));
                i = end;
            } else {
                run = s.mid(i + 1, 1);
                i += 1;
            }
            placeRun(run, script, size, pen, previous, out, missing);
        } else {
            placeRun(QString(c), Script::Normal, size, pen, previous, out, missing);
        }
    }
    return out;
}

void TextLayout::placeRun(const QString &run, Script script, double size, double &pen, QChar &previous,
                          std::vector<Polyline> &out, QString *missing) const
{
    const double effective = script == Script::Normal ? size : size * m_params.scriptScale;
    const double scale = effective / m_font.capHeight();
    const double shift = script == Script::Superscript ? -m_params.superscriptRise * size
                       : script == Script::Subscript  ? m_params.subscriptDrop * size
                                                      : 0.0;

    for (const QChar c : run) {
        const HersheyGlyph *glyph = m_font.glyph(c);
        if (!glyph) {
            if (missing && !missing->contains(c))
                missing->append(c);
            continue;
        }
        pen += kerning(previous, c) * scale;
        for (const Polyline &stroke : glyph->strokes) {
            Polyline placed;
            placed.reserve(stroke.size());
            for (const QPointF &p : stroke)
                placed.emplace_back(pen + (p.x() - glyph->left) * scale,
                                    (p.y() - m_font.baseline()) * scale + shift);
            out.push_back(std::move(placed));
        }
        pen += (glyph->right - glyph->left + m_params.tracking) * scale;
        previous = c;
    }
}

double TextLayout::kerning(QChar left, QChar right) const
{
    if (left.isNull())
        return 0.0;
    for (const char *pair : kKernPairs)
        if (left == QLatin1Char(pair[0]) && right == QLatin1Char(pair[1]))
            return -m_params.kerning;
    return 0.0;
}
