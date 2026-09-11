#include "Geometry2D.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.141592653589793;

double length(const QPointF &p)
{
    return std::hypot(p.x(), p.y());
}

} // namespace

Geometry2D::Geometry2D(const SceneParams &params)
    : m_params(params)
{
}

Polyline Geometry2D::circle(const QPointF &center, double radius) const
{
    return ellipseArc(center, radius, radius, 90.0, 360.0);
}

Polyline Geometry2D::ellipse(const QPointF &center, double radiusX, double radiusY) const
{
    return ellipseArc(center, radiusX, radiusY, 90.0, 360.0);
}

Polyline Geometry2D::arc(const QPointF &center, double radius, double startDeg, double endDeg) const
{
    const double sweep = std::clamp(endDeg - startDeg, -360.0, 360.0);
    return ellipseArc(center, radius, radius, startDeg, sweep);
}

Polyline Geometry2D::ellipseArc(const QPointF &center, double radiusX, double radiusY,
                                double startDeg, double sweepDeg) const
{
    // Amostragem adaptativa: ângulo por segmento tal que a flecha da corda <= tolerância
    const double r = std::max(radiusX, radiusY);
    const double sweep = std::abs(sweepDeg) * kPi / 180.0;
    double maxStep = 2.0 * kPi / m_params.minCurveSegments;
    if (r > m_params.curveTolerance)
        maxStep = std::min(maxStep, 2.0 * std::acos(1.0 - m_params.curveTolerance / r));
    const int segments = std::max(1, static_cast<int>(std::ceil(sweep / maxStep)));

    Polyline points;
    points.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const double a = (startDeg + sweepDeg * i / segments) * kPi / 180.0;
        points.emplace_back(center.x() + radiusX * std::cos(a), center.y() - radiusY * std::sin(a));
    }
    if (std::abs(sweepDeg) >= 360.0)
        points.back() = points.front(); // fecha exatamente
    return points;
}

Polyline Geometry2D::rectangle(const QPointF &center, double width, double height) const
{
    const double l = center.x() - width / 2, r = center.x() + width / 2;
    const double t = center.y() - height / 2, b = center.y() + height / 2;
    return {{l, t}, {r, t}, {r, b}, {l, b}, {l, t}};
}

Polyline Geometry2D::polygon(const QPointF &origin, const std::vector<QPointF> &relativePoints) const
{
    Polyline points;
    points.reserve(relativePoints.size() + 1);
    for (const QPointF &p : relativePoints)
        points.push_back(origin + p);
    if (!points.empty())
        points.push_back(points.front());
    return points;
}

Polyline Geometry2D::line(const QPointF &from, const QPointF &to) const
{
    return {from, to};
}

Polyline Geometry2D::arrowHead(const QPointF &from, const QPointF &tip) const
{
    const QPointF d = tip - from;
    const double len = length(d);
    if (len <= 0.0)
        return {};
    const QPointF back = -d / len * m_params.arrowHeadLength;
    const double a = m_params.arrowHeadAngle * kPi / 180.0;
    const auto rotate = [](const QPointF &v, double angle) {
        return QPointF(v.x() * std::cos(angle) - v.y() * std::sin(angle),
                       v.x() * std::sin(angle) + v.y() * std::cos(angle));
    };
    // Uma asa, a ponta e a outra asa, num único traço
    return {tip + rotate(back, a), tip, tip + rotate(back, -a)};
}

std::vector<Polyline> Geometry2D::styled(const std::vector<Polyline> &lines, LineStyle style) const
{
    switch (style) {
    case LineStyle::Dashed:
        return pattern(lines, m_params.dashLength, m_params.dashGap);
    case LineStyle::Dotted:
        return pattern(lines, m_params.dotLength, m_params.dotGap);
    case LineStyle::Solid:
        break;
    }
    return lines;
}

std::vector<Polyline> Geometry2D::pattern(const std::vector<Polyline> &lines, double on, double off) const
{
    // Percorre cada polilinha pelo comprimento, alternando trechos desenhados (on) e vazios (off)
    std::vector<Polyline> out;
    for (const Polyline &pl : lines) {
        if (pl.size() < 2)
            continue;
        bool drawing = true;
        double remaining = on;
        Polyline current{pl.front()};
        for (std::size_t i = 1; i < pl.size(); ++i) {
            const QPointF a = pl[i - 1];
            const QPointF d = pl[i] - a;
            const double segment = length(d);
            double pos = 0.0;
            while (segment - pos > remaining) {
                pos += remaining;
                const QPointF p = a + d * (pos / segment);
                if (drawing) {
                    current.push_back(p);
                    out.push_back(current);
                    current.clear();
                } else {
                    current = {p};
                }
                drawing = !drawing;
                remaining = drawing ? on : off;
            }
            remaining -= segment - pos;
            if (drawing)
                current.push_back(pl[i]);
        }
        if (drawing && current.size() >= 2)
            out.push_back(current);
    }
    return out;
}

QRectF Geometry2D::bounds(const std::vector<Polyline> &lines)
{
    bool first = true;
    double left = 0, top = 0, right = 0, bottom = 0;
    for (const Polyline &pl : lines)
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
