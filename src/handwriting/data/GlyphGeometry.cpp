#include "GlyphGeometry.h"

#include <cmath>

namespace handwriting::geometry {

namespace {

double distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(b.x() - a.x(), b.y() - a.y());
}

QRectF pointBounds(const std::vector<WritingPoint> &points)
{
    if (points.empty())
        return {};
    double left = points.front().pos.x(), right = left;
    double top = points.front().pos.y(), bottom = top;
    for (const WritingPoint &p : points) {
        left = std::min(left, p.pos.x());
        right = std::max(right, p.pos.x());
        top = std::min(top, p.pos.y());
        bottom = std::max(bottom, p.pos.y());
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

} // namespace

void computeVelocities(std::vector<WritingPoint> &points)
{
    const std::size_t n = points.size();
    if (n < 2) {
        for (WritingPoint &p : points)
            p.velocity = 0.0;
        return;
    }
    double last = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t a = i == 0 ? 0 : i - 1;
        const std::size_t b = i + 1 == n ? n - 1 : i + 1;
        const double dt = points[b].timeMs - points[a].timeMs;
        // Amostras com o mesmo timestamp (comum no mouse) repetem a última velocidade
        if (dt > 1e-9)
            last = distance(points[a].pos, points[b].pos) / dt * 1000.0;
        points[i].velocity = last;
    }
}

StrokeMetrics strokeMetrics(const std::vector<WritingPoint> &points, const HandwritingParams &params)
{
    StrokeMetrics m;
    if (points.empty())
        return m;
    m.start = points.front().pos;
    m.end = points.back().pos;
    m.bounds = pointBounds(points);
    m.durationMs = points.back().timeMs - points.front().timeMs;

    std::vector<double> cumulative(points.size(), 0.0);
    for (std::size_t i = 1; i < points.size(); ++i)
        cumulative[i] = cumulative[i - 1] + distance(points[i - 1].pos, points[i].pos);
    m.length = cumulative.back();
    m.meanVelocity = m.durationMs > 1e-9 ? m.length / m.durationMs * 1000.0 : 0.0;

    const QPointF chord = m.end - m.start;
    m.direction = std::atan2(chord.y(), chord.x());

    // Entrada do traço: do começo até a fração inicial do comprimento
    QPointF entry = m.end;
    const double target = m.length * params.initialDirectionFraction;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (cumulative[i] >= target) {
            entry = points[i].pos;
            break;
        }
    }
    const QPointF initial = entry - m.start;
    m.initialDirection = std::atan2(initial.y(), initial.x());
    return m;
}

void updateDerived(GlyphVariant &variant, const HandwritingParams &params)
{
    GlyphMetrics &g = variant.metrics;
    g = GlyphMetrics();
    bool first = true;
    double previousEnd = 0.0;
    for (Stroke &stroke : variant.strokes) {
        computeVelocities(stroke.points);
        computeVelocities(stroke.rawPoints);
        stroke.metrics = strokeMetrics(stroke.points, params);
        if (stroke.points.empty())
            continue;
        // Sem QRectF::united(): ele ignora caixas de área nula (um ponto, uma reta)
        const QRectF &b = stroke.metrics.bounds;
        if (first) {
            g.bounds = b;
            g.start = stroke.metrics.start;
        } else {
            g.penUpMs += std::max(0.0, stroke.startMs - previousEnd);
        }
        g.bounds.setLeft(std::min(g.bounds.left(), b.left()));
        g.bounds.setTop(std::min(g.bounds.top(), b.top()));
        g.bounds.setRight(std::max(g.bounds.right(), b.right()));
        g.bounds.setBottom(std::max(g.bounds.bottom(), b.bottom()));

        g.end = stroke.metrics.end;
        g.inkMs += stroke.metrics.durationMs;
        g.durationMs = stroke.endMs();
        previousEnd = stroke.endMs();
        first = false;
    }
}

void normalizeLeftEdge(GlyphVariant &variant, const HandwritingParams &params)
{
    updateDerived(variant, params);
    const double left = variant.metrics.bounds.left();
    if (left == 0.0)
        return;
    for (Stroke &stroke : variant.strokes)
        for (WritingPoint &p : stroke.points)
            p.pos.rx() -= left;
    variant.capture.originPx.rx() += left * variant.capture.unitPx;
    updateDerived(variant, params);
}

} // namespace handwriting::geometry
