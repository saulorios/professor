#include "HersheyFont.h"

#include <QFile>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kRowTolerance = 3.0;    // traços que começam a menos disso em Y estão na mesma "linha"
constexpr double kOrientTolerance = 1.0; // desnível mínimo para considerar um traço "de baixo para cima"
constexpr double kMarkGap = 2.0;         // espaço entre a letra e o acento
constexpr double kDegreeRadius = 2.5;    // raio do símbolo de grau
constexpr double kDegreeHalfWidth = 5.0; // meia largura do glifo de grau

enum class Mark { Acute, Grave, Circumflex, Tilde, Cedilla };

// Diacríticos em unidades da fonte, relativos à âncora (x no centro da letra;
// y na base do acento, negativo para cima). Já na ordem natural de traçado.
Polyline markShape(Mark mark)
{
    switch (mark) {
    case Mark::Acute:
        return {{2.0, -3.5}, {-1.0, 0.0}};
    case Mark::Grave:
        return {{-2.0, -3.5}, {1.0, 0.0}};
    case Mark::Circumflex:
        return {{-3.0, 0.0}, {0.0, -3.5}, {3.0, 0.0}};
    case Mark::Tilde:
        return {{-3.5, -0.6}, {-2.5, -2.0}, {-1.2, -2.4}, {0.0, -1.6}, {1.2, -0.8}, {2.5, -0.8}, {3.5, -2.2}};
    case Mark::Cedilla: // âncora na base da letra; o gancho desce
        return {{0.0, 0.0}, {0.0, 1.8}, {1.6, 2.4}, {1.6, 3.6}, {0.0, 4.2}, {-1.2, 3.9}};
    }
    return {};
}

// Tabela de fallback: caractere → letra base + diacrítico
struct Composite {
    char16_t character;
    char base;
    Mark mark;
};

const Composite kComposites[] = {
    {u'á', 'a', Mark::Acute},      // á
    {u'à', 'a', Mark::Grave},      // à
    {u'â', 'a', Mark::Circumflex}, // â
    {u'ã', 'a', Mark::Tilde},      // ã
    {u'é', 'e', Mark::Acute},      // é
    {u'ê', 'e', Mark::Circumflex}, // ê
    {u'í', 'i', Mark::Acute},      // í
    {u'ó', 'o', Mark::Acute},      // ó
    {u'ô', 'o', Mark::Circumflex}, // ô
    {u'õ', 'o', Mark::Tilde},      // õ
    {u'ú', 'u', Mark::Acute},      // ú
    {u'ç', 'c', Mark::Cedilla},    // ç
    {u'Á', 'A', Mark::Acute},      // Á
    {u'À', 'A', Mark::Grave},      // À
    {u'Â', 'A', Mark::Circumflex}, // Â
    {u'Ã', 'A', Mark::Tilde},      // Ã
    {u'É', 'E', Mark::Acute},      // É
    {u'Ê', 'E', Mark::Circumflex}, // Ê
    {u'Í', 'I', Mark::Acute},      // Í
    {u'Ó', 'O', Mark::Acute},      // Ó
    {u'Ô', 'O', Mark::Circumflex}, // Ô
    {u'Õ', 'O', Mark::Tilde},      // Õ
    {u'Ú', 'U', Mark::Acute},      // Ú
    {u'Ç', 'C', Mark::Cedilla},    // Ç
};

QRectF inkBounds(const std::vector<Polyline> &strokes)
{
    bool first = true;
    double left = 0, top = 0, right = 0, bottom = 0;
    for (const Polyline &pl : strokes)
        for (const QPointF &p : pl) {
            if (first) {
                left = right = p.x();
                top = bottom = p.y();
                first = false;
            }
            left = std::min(left, p.x());
            right = std::max(right, p.x());
            top = std::min(top, p.y());
            bottom = std::max(bottom, p.y());
        }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

// Círculo fechado começando no topo, no sentido anti-horário
Polyline circle(const QPointF &center, double radius)
{
    constexpr int kSegments = 12;
    constexpr double kTwoPi = 6.283185307179586;
    Polyline points;
    for (int i = 0; i <= kSegments; ++i) {
        const double a = kTwoPi * i / kSegments;
        points.emplace_back(center.x() - radius * std::sin(a), center.y() - radius * std::cos(a));
    }
    points.back() = points.front();
    return points;
}

} // namespace

bool HersheyFont::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QString("não foi possível abrir %1: %2").arg(path, file.errorString());
        return false;
    }
    QByteArray data = file.readAll();
    data.replace('\r', "");

    // Registro: número do glifo (5), quantidade de pares (3) e os pares, que
    // podem continuar na linha seguinte. O primeiro par são os limites esquerdo/direito.
    m_glyphs.clear();
    qsizetype pos = 0;
    int index = 0;
    while (pos < data.size()) {
        while (pos < data.size() && data[pos] == '\n')
            ++pos;
        if (pos + 8 > data.size())
            break;
        const int count = data.mid(pos + 5, 3).trimmed().toInt();
        pos += 8;
        QByteArray pairs;
        while (pairs.size() < 2 * count && pos < data.size()) {
            const char ch = data[pos++];
            if (ch != '\n')
                pairs.append(ch);
        }
        if (count < 1 || pairs.size() < 2 * count) {
            if (error)
                *error = QString("glifo %1 incompleto em %2").arg(index).arg(path);
            m_glyphs.clear();
            return false;
        }

        HersheyGlyph glyph;
        glyph.left = pairs[0] - 'R';
        glyph.right = pairs[1] - 'R';
        Polyline stroke;
        for (int i = 1; i < count; ++i) {
            const char a = pairs[2 * i], b = pairs[2 * i + 1];
            if (a == ' ' && b == 'R') { // caneta levanta
                if (stroke.size() >= 2)
                    glyph.strokes.push_back(stroke);
                stroke.clear();
                continue;
            }
            stroke.emplace_back(a - 'R', b - 'R');
        }
        if (stroke.size() >= 2)
            glyph.strokes.push_back(stroke);

        normalizeStrokeOrder(glyph);
        m_glyphs.insert(QChar(char16_t(32 + index)), glyph);
        ++index;
    }

    // Métricas medidas nos próprios glifos
    if (m_glyphs.contains('H') && m_glyphs.contains('x')) {
        const QRectF cap = inkBounds(m_glyphs['H'].strokes);
        const QRectF lower = inkBounds(m_glyphs['x'].strokes);
        m_capTop = cap.top();
        m_baseline = cap.bottom();
        m_xTop = lower.top();
    }

    addComposites();
    return !m_glyphs.isEmpty();
}

const HersheyGlyph *HersheyFont::glyph(QChar c) const
{
    const auto it = m_glyphs.constFind(c);
    return it == m_glyphs.constEnd() ? nullptr : &it.value();
}

void HersheyFont::normalizeStrokeOrder(HersheyGlyph &glyph)
{
    // Cada traço aberto começa na ponta de cima (ou na da esquerda, se estiver na horizontal)
    for (Polyline &stroke : glyph.strokes) {
        const QPointF a = stroke.front(), b = stroke.back();
        const bool closed = a == b;
        const double rise = a.y() - b.y(); // > 0: termina mais alto do que começa
        if (!closed && (rise > kOrientTolerance || (std::abs(rise) <= kOrientTolerance && b.x() < a.x())))
            std::reverse(stroke.begin(), stroke.end());
    }
    // Traços de cima primeiro; na mesma altura, da esquerda para a direita
    std::stable_sort(glyph.strokes.begin(), glyph.strokes.end(), [](const Polyline &p, const Polyline &q) {
        const long rowP = std::lround(p.front().y() / kRowTolerance);
        const long rowQ = std::lround(q.front().y() / kRowTolerance);
        if (rowP != rowQ)
            return rowP < rowQ;
        return p.front().x() < q.front().x();
    });
}

void HersheyFont::addComposites()
{
    for (const Composite &composite : kComposites) {
        const QChar character(composite.character);
        const QChar baseChar = QLatin1Char(composite.base);
        if (m_glyphs.contains(character) || !m_glyphs.contains(baseChar))
            continue;

        HersheyGlyph glyph = m_glyphs[baseChar];
        const double markBottom = (baseChar.isUpper() ? m_capTop : m_xTop) - kMarkGap;

        // "i" sem o pingo: remove os traços que ficam inteiros acima das minúsculas
        if (composite.base == 'i') {
            const double xTop = m_xTop;
            glyph.strokes.erase(std::remove_if(glyph.strokes.begin(), glyph.strokes.end(),
                                               [xTop](const Polyline &pl) {
                                                   return std::all_of(pl.begin(), pl.end(),
                                                                      [xTop](const QPointF &p) { return p.y() < xTop; });
                                               }),
                                glyph.strokes.end());
        }

        const QRectF ink = inkBounds(glyph.strokes);
        const QPointF anchor(ink.center().x(), composite.mark == Mark::Cedilla ? m_baseline : markBottom);
        Polyline mark = markShape(composite.mark);
        for (QPointF &p : mark)
            p += anchor;
        glyph.strokes.push_back(mark); // o acento vem depois da letra, como à mão
        m_glyphs.insert(character, glyph);
    }

    // Grau (°): pequeno círculo no alto
    const QChar degree(u'°');
    if (!m_glyphs.contains(degree)) {
        HersheyGlyph glyph;
        glyph.left = -kDegreeHalfWidth;
        glyph.right = kDegreeHalfWidth;
        glyph.strokes.push_back(circle(QPointF(0.0, m_capTop + kDegreeRadius), kDegreeRadius));
        m_glyphs.insert(degree, glyph);
    }

    // Ponto de multiplicação (·): o ponto final erguido até o meio das minúsculas
    const QChar middleDot(u'·');
    if (!m_glyphs.contains(middleDot) && m_glyphs.contains('.')) {
        HersheyGlyph glyph = m_glyphs['.'];
        const double dy = (m_xTop + m_baseline) / 2.0 - inkBounds(glyph.strokes).center().y();
        for (Polyline &pl : glyph.strokes)
            for (QPointF &p : pl)
                p.ry() += dy;
        m_glyphs.insert(middleDot, glyph);
    }
}
