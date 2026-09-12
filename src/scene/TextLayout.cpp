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

std::vector<Polyline> TextLayout::layout(const QString &text, double size, QString *missing,
                                         std::vector<GlyphRun> *runs) const
{
    // Forma composta (NFC): "a" + acento combinante vira "á"
    const QString s = text.normalized(QString::NormalizationForm_C);
    std::vector<Polyline> out;
    Cursor cursor;

    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (i > 0)
            cursor.rawPrevious = s[i - 1];
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
            placeRun(run, script, size, cursor, out, missing, runs);
        } else {
            placeRun(QString(c), Script::Normal, size, cursor, out, missing, runs);
        }
    }
    return out;
}

void TextLayout::placeRun(const QString &run, Script script, double size, Cursor &cursor,
                          std::vector<Polyline> &out, QString *missing, std::vector<GlyphRun> *runs) const
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
        cursor.pen += kerning(cursor.previous, c) * scale;
        const std::size_t first = out.size();
        for (const Polyline &stroke : glyph->strokes) {
            Polyline placed;
            placed.reserve(stroke.size());
            for (const QPointF &p : stroke)
                placed.emplace_back(cursor.pen + (p.x() - glyph->left) * scale,
                                    (p.y() - m_font.baseline()) * scale + shift);
            out.push_back(std::move(placed));
        }
        if (runs && out.size() > first)
            runs->push_back({c, cursor.rawPrevious, cursor.index, first, out.size() - first,
                             QPointF(cursor.pen, shift), scale});
        cursor.pen += (glyph->right - glyph->left + m_params.tracking) * scale;
        cursor.previous = c;
        cursor.rawPrevious = c;
        ++cursor.index;
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
