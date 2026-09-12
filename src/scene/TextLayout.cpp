#include "TextLayout.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Pares que ficam visualmente afastados demais só com os limites do glifo
const char *const kKernPairs[] = {
    "AV", "AW", "AY", "AT", "VA", "WA", "YA", "TA", "LT", "LV", "LW", "LY",
    "Ta", "Te", "To", "Tr", "Tu", "Ty", "Va", "Ve", "Vo", "Wa", "We", "Wo",
    "Ya", "Ye", "Yo", "P.", "P,", "T.", "T,", "V.", "V,", "W.", "W,", "Y.",
    "Y,", "F.", "F,", "r.", "r,",
};

// Operadores de nível superior: a fórmula pode quebrar antes deles
bool isOperator(QChar c)
{
    static const QString operators = QString::fromUtf8("=+-*/·×÷±<>≤≥≠⇔⇒→↔");
    return operators.contains(c);
}

} // namespace

TextLayout::TextLayout(const HersheyFont &font, const SceneParams &params)
    : m_font(font)
    , m_params(params)
{
}

double TextLayout::advance(QChar character, Script script, double size) const
{
    const HersheyGlyph *glyph = m_font.glyph(character);
    if (!glyph)
        return 0.0;
    const double effective = script == Script::Normal ? size : size * m_params.scriptScale;
    const double scale = effective / m_font.capHeight();
    return (glyph->right - glyph->left + m_params.tracking) * scale;
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

std::vector<TextLayout::Unit> TextLayout::parse(const QString &text) const
{
    // Forma composta (NFC): "a" + acento combinante vira "á"
    const QString s = text.normalized(QString::NormalizationForm_C);
    std::vector<Unit> units;

    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if ((c == '_' || c == '^') && i + 1 < s.size() && !units.empty()) {
            // Índice ou expoente: cola no caractere anterior e nunca se separa dele
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
            for (const QChar r : run)
                units.back().atoms.push_back({r, script});
            continue;
        }
        Unit unit;
        unit.atoms.push_back({c, Script::Normal});
        unit.space = c.isSpace();
        unit.op = isOperator(c);
        unit.letter = c.isLetter();
        units.push_back(std::move(unit));
    }
    return units;
}

std::vector<TextLayout::Group> TextLayout::group(const std::vector<Unit> &units) const
{
    // Uma "palavra": só se pode quebrar depois de um espaço ou antes de um operador
    std::vector<Group> groups;
    for (std::size_t i = 0; i < units.size(); ++i) {
        const bool afterSpace = i > 0 && units[i - 1].space;
        const bool operatorHere = units[i].op && i > 0;
        if (groups.empty() || afterSpace || operatorHere || units[i].space) {
            Group group;
            group.startsWithOperator = operatorHere;
            groups.push_back(std::move(group));
        }
        groups.back().units.push_back(units[i]);
        if (units[i].space && i + 1 < units.size()) {
            Group group; // o que vem depois do espaço começa outra palavra
            groups.push_back(std::move(group));
        }
    }
    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [](const Group &g) { return g.units.empty(); }),
                 groups.end());
    return groups;
}

QRectF TextLayout::emitLine(const std::vector<Group> &groups, double size, double baseline, int line,
                            int &glyphIndex, QChar &previous, TextBlock *block) const
{
    double pen = 0.0;
    QChar kernPrevious;   // o kerning não atravessa a quebra de linha
    QChar rawPrevious = previous;
    bool first = true;
    double left = 0.0, right = 0.0, top = baseline, bottom = baseline;

    for (const Group &group : groups) {
        const double groupSize = size * group.scale;
        for (const Unit &unit : group.units) {
            for (const Atom &atom : unit.atoms) {
                const HersheyGlyph *glyph = m_font.glyph(atom.character);
                if (!glyph) {
                    if (!atom.character.isSpace() && !block->missing.contains(atom.character))
                        block->missing.append(atom.character);
                    rawPrevious = atom.character;
                    continue;
                }
                const double effective = atom.script == Script::Normal ? groupSize
                                                                       : groupSize * m_params.scriptScale;
                const double scale = effective / m_font.capHeight();
                const double shift = atom.script == Script::Superscript ? -m_params.superscriptRise * groupSize
                                   : atom.script == Script::Subscript  ? m_params.subscriptDrop * groupSize
                                                                       : 0.0;
                pen += kerning(kernPrevious, atom.character) * scale;

                const std::size_t begin = block->strokes.size();
                for (const Polyline &stroke : glyph->strokes) {
                    Polyline placed;
                    placed.reserve(stroke.size());
                    for (const QPointF &p : stroke) {
                        const QPointF point(pen + (p.x() - glyph->left) * scale,
                                            (p.y() - m_font.baseline()) * scale + shift + baseline);
                        placed.push_back(point);
                        left = first ? point.x() : std::min(left, point.x());
                        right = first ? point.x() : std::max(right, point.x());
                        top = std::min(top, point.y());
                        bottom = std::max(bottom, point.y());
                        first = false;
                    }
                    block->strokes.push_back(std::move(placed));
                }
                if (block->strokes.size() > begin)
                    block->runs.push_back({atom.character, rawPrevious, glyphIndex, line, begin,
                                           block->strokes.size() - begin, QPointF(pen, baseline), scale});
                pen += (glyph->right - glyph->left + m_params.tracking) * scale;
                kernPrevious = atom.character;
                rawPrevious = atom.character;
                previous = atom.character;
                ++glyphIndex;
            }
        }
    }
    return first ? QRectF() : QRectF(QPointF(left, top), QPointF(right, bottom));
}

TextBlock TextLayout::layout(const QString &text, double size, double maxWidth) const
{
    TextBlock block;
    std::vector<Unit> units = parse(text);
    for (Unit &unit : units) {
        unit.width = 0.0;
        for (const Atom &atom : unit.atoms)
            unit.width += advance(atom.character, atom.script, size);
    }
    std::vector<Group> groups = group(units);
    if (groups.empty())
        return block;

    // Largura de cada palavra (o kerning entre palavras é desprezível aqui)
    const auto measure = [this, size](Group &group) {
        group.width = 0.0;
        QChar previous;
        for (const Unit &unit : group.units)
            for (const Atom &atom : unit.atoms) {
                group.width += kerning(previous, atom.character) * (size * group.scale / m_font.capHeight());
                group.width += advance(atom.character, atom.script, size * group.scale);
                previous = atom.character;
            }
    };
    for (Group &group : groups)
        measure(group);

    // --- Quebra em linhas ---
    std::vector<std::vector<Group>> lines;
    lines.emplace_back();
    double used = 0.0;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        Group group = groups[i];

        // Espaço no começo da linha não conta
        if (lines.back().empty() && group.units.size() == 1 && group.units.front().space)
            continue;

        if (maxWidth > 0.0 && group.width > maxWidth && lines.back().empty()) {
            // A palavra sozinha não cabe: hífen entre letras; se nem isso, encolhe
            const bool hyphenatable = group.units.size() > 1
                                      && std::all_of(group.units.begin(), group.units.end(),
                                                     [](const Unit &u) { return u.letter; });
            if (hyphenatable) {
                Group head;
                const HersheyGlyph *hyphen = m_font.glyph('-');
                const double hyphenWidth = hyphen ? advance('-', Script::Normal, size) : 0.0;
                std::size_t taken = 0;
                double width = 0.0;
                for (const Unit &unit : group.units) {
                    if (width + unit.width + hyphenWidth > maxWidth && taken > 0)
                        break;
                    head.units.push_back(unit);
                    width += unit.width;
                    ++taken;
                }
                if (taken > 0 && taken < group.units.size()) {
                    Unit dash;
                    dash.atoms.push_back({QChar('-'), Script::Normal});
                    head.units.push_back(dash);
                    measure(head);
                    lines.back().push_back(head);
                    Group tail;
                    tail.units.assign(group.units.begin() + std::ptrdiff_t(taken), group.units.end());
                    measure(tail);
                    groups[i] = tail;
                    lines.emplace_back();
                    used = 0.0;
                    --i; // a sobra tenta de novo, agora numa linha vazia
                    continue;
                }
            }
            // Palavra indivisível maior que a coluna: encolhe só ela
            group.scale = std::max(m_params.minScale, maxWidth / group.width);
            measure(group);
            block.shrunk = true;
        }

        if (maxWidth > 0.0 && !lines.back().empty() && used + group.width > maxWidth) {
            // Quebra antes de um operador: ele fica também no fim da linha anterior
            if (group.startsWithOperator) {
                Group repeated;
                repeated.units.push_back(group.units.front());
                measure(repeated);
                lines.back().push_back(repeated);
            }
            lines.emplace_back();
            used = 0.0;
            if (group.units.size() == 1 && group.units.front().space)
                continue;
        }
        used += group.width;
        lines.back().push_back(group);
    }

    // --- Coloca linha por linha ---
    const double lineHeight = size * m_params.lineSpacing;
    int glyphIndex = 0;
    QChar previous;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty())
            continue;
        const QRectF box = emitLine(lines[i], size, double(i) * lineHeight, int(i), glyphIndex, previous, &block);
        if (!box.isNull())
            block.lines.push_back(box);
    }
    return block;
}
