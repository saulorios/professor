#include "Humanizer.h"

#include "physics/Noise.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kClosed = 1e-9;   // ponta que volta exatamente ao início
constexpr double kMinTurn = 0.45;  // ~26°: abaixo disso a quina nem aparece

double length(const QPointF &p)
{
    return std::hypot(p.x(), p.y());
}

// Sorteio determinístico em [-1, 1] para esta ocorrência deste caractere
double jitter(QChar character, int index, int salt, std::uint32_t seed)
{
    return 2.0 * double(noise::value(std::int32_t(character.unicode()) * 131 + salt, index, seed)) - 1.0;
}

// Sorteio em [0, 1)
double chance(QChar character, int index, int salt, std::uint32_t seed)
{
    return double(noise::value(std::int32_t(character.unicode()) * 131 + salt, index, seed));
}

// Ruído 1D suave ao longo da frase: letras vizinhas sobem e descem juntas, e a
// linha de base fica ondulada em vez de serrilhada
double waveAt(double x, std::uint32_t seed)
{
    const double fx = std::floor(x);
    const auto i = std::int32_t(fx);
    double t = x - fx;
    t = t * t * (3.0 - 2.0 * t);
    const double a = double(noise::value(i, 991, seed));
    const double b = double(noise::value(i + 1, 991, seed));
    return 2.0 * (a + (b - a) * t) - 1.0;
}

// Segmentos retos longos viram uma curva muito suave (é o que mais tira a
// aparência de plotter); os curtos e as curvas já existentes ficam como estão
Polyline bow(const Polyline &line, double amount, double minSegment, int pieces, QChar c, int index,
             std::uint32_t seed)
{
    if (line.size() < 2 || amount <= 0.0)
        return line;
    Polyline out;
    out.push_back(line.front());
    for (std::size_t i = 1; i < line.size(); ++i) {
        const QPointF a = line[i - 1], b = line[i];
        const QPointF d = b - a;
        const double len = length(d);
        if (len < minSegment) {
            out.push_back(b);
            continue;
        }
        const QPointF normal(-d.y() / len, d.x() / len);
        const double sagitta = amount * len * jitter(c, index, 300 + int(i), seed);
        for (int k = 1; k <= pieces; ++k) {
            const double t = double(k) / pieces;
            // Parábola: zero nas pontas, máximo no meio
            out.push_back(a + d * t + normal * (sagitta * 4.0 * t * (1.0 - t)));
        }
    }
    return out;
}

// A mão não faz quina perfeita: arredonda os vértices dentro do mesmo traço
Polyline roundCorners(const Polyline &line, double radius)
{
    if (line.size() < 3 || radius <= 0.0)
        return line;
    Polyline out;
    out.push_back(line.front());
    for (std::size_t i = 1; i + 1 < line.size(); ++i) {
        const QPointF v = line[i];
        const QPointF in = line[i - 1] - v, next = line[i + 1] - v;
        const double li = length(in), ln = length(next);
        if (li < 1e-9 || ln < 1e-9) {
            out.push_back(v);
            continue;
        }
        const QPointF ui = in / li, un = next / ln;
        const double cosTurn = ui.x() * un.x() + ui.y() * un.y();
        if (cosTurn < -1.0 + kMinTurn * kMinTurn / 2.0) { // quase reto: nada a arredondar
            out.push_back(v);
            continue;
        }
        const double r = std::min({radius, 0.4 * li, 0.4 * ln});
        const QPointF p1 = v + ui * r, p2 = v + un * r;
        out.push_back(p1);
        out.push_back(p1 * 0.25 + v * 0.5 + p2 * 0.25); // meio da curva de Bézier
        out.push_back(p2);
    }
    out.push_back(line.back());
    return out;
}

// Traço fechado (O, o, D, 0): sobra ou falta um pedacinho no ponto de fecho
void imperfectClose(Polyline &line, double fraction)
{
    if (line.size() < 3 || fraction == 0.0 || length(line.back() - line.front()) > kClosed)
        return;
    double perimeter = 0.0;
    for (std::size_t i = 1; i < line.size(); ++i)
        perimeter += length(line[i] - line[i - 1]);
    double delta = fraction * perimeter;

    if (delta > 0.0) {
        // Passa do início, seguindo pelo começo do próprio traço
        for (std::size_t i = 1; i < line.size() && delta > 0.0; ++i) {
            const QPointF d = line[i] - line[i - 1];
            const double len = length(d);
            if (len <= 0.0)
                continue;
            const double step = std::min(delta, len);
            line.push_back(line[i - 1] + d * (step / len));
            delta -= step;
        }
        return;
    }
    // Falha: para um pouco antes de fechar
    double missing = -delta;
    while (line.size() > 2 && missing > 0.0) {
        const double len = length(line.back() - line[line.size() - 2]);
        if (len <= missing) {
            line.pop_back();
            missing -= len;
        } else {
            const QPointF d = line[line.size() - 2] - line.back();
            line.back() += d * (missing / len);
            break;
        }
    }
}

// Distância de um ponto ao traço inteiro
double distanceTo(const QPointF &p, const Polyline &line)
{
    double best = 1e18;
    for (std::size_t i = 1; i < line.size(); ++i) {
        const QPointF a = line[i - 1], d = line[i] - a;
        const double len2 = d.x() * d.x() + d.y() * d.y();
        double t = 0.0;
        if (len2 > 0.0)
            t = std::clamp(((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / len2, 0.0, 1.0);
        best = std::min(best, length(p - (a + d * t)));
    }
    return best;
}

} // namespace

Humanizer::Humanizer(const HumanizerParams &params)
    : m_params(params)
{
}

std::vector<HandStroke> Humanizer::apply(const std::vector<Polyline> &strokes, const std::vector<GlyphRun> &runs,
                                         PressureLevel pressure, bool cursive) const
{
    std::vector<HandStroke> out;
    out.reserve(strokes.size());
    for (const Polyline &stroke : strokes)
        out.push_back({stroke, pressure, 1.0, 1.0, 0.0});

    // A mão respira ao passar para a linha seguinte (isto não depende da intensidade)
    for (std::size_t i = 1; i < runs.size(); ++i)
        if (runs[i].line != runs[i - 1].line && runs[i].first < out.size())
            out[runs[i].first].pauseBeforeMs = m_params.pauseLine;

    const double k = std::clamp(m_params.intensity, 0.0, 1.0) * (cursive ? m_params.cursiveScale : 1.0);
    if (k <= 0.0)
        return out; // intensidade 0: exatamente o que veio da fonte

    const std::uint32_t seed = m_params.seed;
    for (const GlyphRun &run : runs) {
        if (run.first + run.count > out.size())
            continue;
        const QChar c = run.character;
        const int i = run.index;

        // --- Forma dos traços ---
        for (std::size_t s = run.first; s < run.first + run.count; ++s) {
            Polyline &line = out[s].points;
            const bool closed = line.size() > 2 && length(line.back() - line.front()) <= kClosed;
            line = bow(line, m_params.bowing * k, m_params.minSegment, m_params.bowSegments, c,
                       i * 16 + int(s - run.first), seed);
            line = roundCorners(line, m_params.cornerRadius * k);
            if (closed)
                imperfectClose(line, m_params.closeGap * k * jitter(c, i, 400 + int(s), seed));
        }

        // --- Extrapolação: quem encosta em outro traço passa um pouco ---
        const double touching = 1.5 * run.scale; // ~1,5 unidade da fonte
        for (std::size_t s = run.first; s < run.first + run.count; ++s) {
            Polyline &line = out[s].points;
            if (line.size() < 2 || length(line.back() - line.front()) <= kClosed)
                continue;
            double total = 0.0;
            for (std::size_t p = 1; p < line.size(); ++p)
                total += length(line[p] - line[p - 1]);
            for (int end = 0; end < 2; ++end) {
                const QPointF tip = end == 0 ? line.front() : line.back();
                bool touches = false;
                for (std::size_t o = run.first; o < run.first + run.count && !touches; ++o)
                    touches = o != s && distanceTo(tip, out[o].points) < touching;
                if (!touches)
                    continue;
                const QPointF from = end == 0 ? line[1] : line[line.size() - 2];
                const double len = length(tip - from);
                if (len < 1e-9)
                    continue;
                const QPointF step = (tip - from) / len * (m_params.overshoot * k * total
                                                           * chance(c, i, 500 + end, seed));
                if (end == 0)
                    line.insert(line.begin(), tip + step);
                else
                    line.push_back(tip + step);
            }
        }

        // --- Escala, rotação e linha de base, por letra ---
        double left = 0, right = 0;
        bool first = true;
        for (std::size_t s = run.first; s < run.first + run.count; ++s)
            for (const QPointF &p : out[s].points) {
                left = first ? p.x() : std::min(left, p.x());
                right = first ? p.x() : std::max(right, p.x());
                first = false;
            }
        const QPointF pivot((left + right) / 2.0, run.origin.y()); // a base da letra
        const double sx = 1.0 + m_params.scaleJitter * k * jitter(c, i, 1, seed);
        const double sy = 1.0 + m_params.scaleJitter * k * jitter(c, i, 2, seed);
        const double angle = m_params.rotationDegrees * k * jitter(c, i, 3, seed) * kPi / 180.0;
        const double dy = m_params.baselineJitter * k
                          * waveAt(double(i) / std::max(m_params.baselineWavelength, 0.1), seed);
        const double cs = std::cos(angle), sn = std::sin(angle);
        for (std::size_t s = run.first; s < run.first + run.count; ++s)
            for (QPointF &p : out[s].points) {
                const double x = (p.x() - pivot.x()) * sx;
                const double y = (p.y() - pivot.y()) * sy;
                p = QPointF(pivot.x() + x * cs - y * sn, pivot.y() + x * sn + y * cs + dy);
            }

        // --- Ritmo: velocidade entre letras e micro-pausas ---
        const double speed = 1.0 + m_params.speedJitter * k * jitter(c, i, 4, seed);
        for (std::size_t s = run.first; s < run.first + run.count; ++s) {
            out[s].speedScale = speed;
            // Vez por outra um traço sai mais rápido e mais leve
            if (chance(c, i, 600 + int(s - run.first), seed) < m_params.quickChance * k) {
                out[s].speedScale *= m_params.quickSpeed;
                out[s].pressureScale = m_params.quickPressure;
            }
        }
        // A mão respira depois da pontuação e entre as palavras
        const QChar before = run.previous;
        double pause = 0.0;
        if (before == ' ')
            pause = m_params.pauseWord;
        else if (before == ',' || before == ';' || before == ':')
            pause = m_params.pauseComma;
        else if (before == '.' || before == '!' || before == '?')
            pause = m_params.pauseStop;
        out[run.first].pauseBeforeMs = std::max(out[run.first].pauseBeforeMs, pause * k);
    }
    return out;
}
