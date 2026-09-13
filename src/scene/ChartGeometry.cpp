#include "ChartGeometry.h"

#include <algorithm>
#include <cmath>

namespace chart {

QPointF Frame::map(double x, double y) const
{
    return QPointF(plot.left() + (x - xMin) / (xMax - xMin) * plot.width(),
                   plot.bottom() - (y - yMin) / (yMax - yMin) * plot.height());
}

double niceStep(double min, double max, int target)
{
    const double span = max - min;
    if (!(span > 0.0) || target < 1)
        return 1.0;
    const double raw = span / target;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double normalized = raw / magnitude;
    const double nice = normalized < 1.5 ? 1.0 : normalized < 3.0 ? 2.0 : normalized < 7.0 ? 5.0 : 10.0;
    return nice * magnitude;
}

std::vector<double> ticks(double min, double max, double step)
{
    std::vector<double> values;
    if (!(step > 0.0))
        return values;
    const double epsilon = step * 1e-9;
    for (double v = std::ceil((min - epsilon) / step) * step; v <= max + epsilon; v += step)
        values.push_back(std::abs(v) < epsilon ? 0.0 : v);
    return values;
}

bool autoRange(const std::vector<const Expression *> &functions, double xMin, double xMax, int samples,
               double *yMin, double *yMax)
{
    std::vector<double> values;
    for (const Expression *function : functions)
        for (int i = 0; i <= samples; ++i) {
            const double y = function->evaluate(xMin + (xMax - xMin) * i / samples);
            if (std::isfinite(y))
                values.push_back(y);
        }
    if (values.empty())
        return false;

    // Ignora os 3% de cada ponta: uma assíntota não achata o resto do gráfico
    std::sort(values.begin(), values.end());
    const std::size_t cut = values.size() * 3 / 100;
    double low = values[cut];
    double high = values[values.size() - 1 - cut];
    if (high - low < 1e-9) {
        low -= 1.0;
        high += 1.0;
    }
    // O eixo x aparece se o zero não estiver longe demais
    const double span = high - low;
    if (low > 0.0 && low < span)
        low = 0.0;
    if (high < 0.0 && -high < span)
        high = 0.0;
    // Folga pequena, sem arredondar para o passo: a faixa fica justa e as
    // marcações (sempre redondas) caem dentro dela
    const double padding = (high - low) * 0.08;
    *yMin = low == 0.0 ? 0.0 : low - padding;
    *yMax = high == 0.0 ? 0.0 : high + padding;
    return *yMax > *yMin;
}

std::vector<Polyline> sample(const Expression &function, const Frame &frame, int samples, double jumpFraction)
{
    std::vector<Polyline> pieces;
    Polyline current;
    const auto flush = [&] {
        if (current.size() >= 2)
            pieces.push_back(current);
        current.clear();
    };
    const double range = frame.yMax - frame.yMin;
    const auto inside = [&](double y) { return std::isfinite(y) && y >= frame.yMin && y <= frame.yMax; };

    double previousX = frame.xMin;
    double previousY = function.evaluate(previousX);
    if (inside(previousY))
        current.push_back(frame.map(previousX, previousY));

    for (int i = 1; i <= samples; ++i) {
        const double x = frame.xMin + (frame.xMax - frame.xMin) * i / samples;
        const double y = function.evaluate(x);
        const bool finite = std::isfinite(y) && std::isfinite(previousY);

        if (!finite || std::abs(y - previousY) > range * jumpFraction) {
            // Função indefinida ou salto de assíntota (tan, 1/x): nada de ligar os dois lados
            flush();
            if (inside(y))
                current.push_back(frame.map(x, y));
        } else if (inside(previousY) && inside(y)) {
            current.push_back(frame.map(x, y));
        } else if (inside(previousY) != inside(y) || (previousY - frame.yMax) * (y - frame.yMax) < 0.0
                   || (previousY - frame.yMin) * (y - frame.yMin) < 0.0) {
            // Atravessa a borda: corta exatamente onde cruza (entrando, saindo ou os dois)
            const double edges[2] = {frame.yMin, frame.yMax};
            std::vector<double> crossings;
            for (double edge : edges)
                if ((previousY - edge) * (y - edge) < 0.0)
                    crossings.push_back((edge - previousY) / (y - previousY));
            std::sort(crossings.begin(), crossings.end());
            bool in = inside(previousY);
            for (double t : crossings) {
                const QPointF at = frame.map(previousX + (x - previousX) * t,
                                             previousY + (y - previousY) * t);
                current.push_back(at);
                if (in)
                    flush();
                in = !in;
            }
            if (inside(y))
                current.push_back(frame.map(x, y));
        }
        previousX = x;
        previousY = y;
    }
    flush();
    return pieces;
}

QString format(double value, double step)
{
    // Casas decimais suficientes para o passo das marcações (0,5 → 1 casa)
    int decimals = 0;
    while (decimals < 6 && std::abs(step * std::pow(10.0, decimals) - std::round(step * std::pow(10.0, decimals))) > 1e-6)
        ++decimals;
    if (std::abs(value) < step * 1e-9)
        value = 0.0;
    QString text = QString::number(value, 'f', decimals);
    text.replace('.', ',');
    return text;
}

} // namespace chart
