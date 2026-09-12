#include "HandwritingEngine.h"

#include "handwriting/data/GlyphDatabase.h"

#include <algorithm>
#include <cmath>

namespace handwriting {

namespace {

constexpr double kPi = 3.141592653589793;

// Hash inteiro determinístico (nada de rand()): mesma seed, mesmas variantes
std::uint32_t mix(std::uint32_t seed, std::uint32_t index)
{
    std::uint32_t h = seed * 0x9E3779B1u ^ (index + 0x7F4A7C15u);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

void extend(QRectF &bounds, bool &empty, const QPointF &p)
{
    if (empty) {
        bounds = QRectF(p, QSizeF(0.0, 0.0));
        empty = false;
        return;
    }
    bounds.setLeft(std::min(bounds.left(), p.x()));
    bounds.setTop(std::min(bounds.top(), p.y()));
    bounds.setRight(std::max(bounds.right(), p.x()));
    bounds.setBottom(std::max(bounds.bottom(), p.y()));
}

// Velocidade e direção de cada amostra de um trecho, já na lousa e no tempo final
void deriveMotion(std::vector<TrajectoryPoint> &points, std::size_t first, std::size_t count)
{
    double velocity = 0.0;
    double direction = 0.0;
    bool haveDirection = false;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t a = first + (i == 0 ? 0 : i - 1);
        const std::size_t b = first + (i + 1 == count ? count - 1 : i + 1);
        const QPointF d = points[b].pos - points[a].pos;
        const double dt = points[b].timeMs - points[a].timeMs;
        if (dt > 1e-9)
            velocity = std::hypot(d.x(), d.y()) / dt * 1000.0;
        if (d.x() != 0.0 || d.y() != 0.0) {
            direction = std::atan2(d.y(), d.x());
            if (!haveDirection) {
                // As amostras paradas do começo herdam a primeira direção real
                for (std::size_t j = first; j < first + i; ++j)
                    points[j].direction = direction;
                haveDirection = true;
            }
        }
        points[first + i].velocity = velocity;
        points[first + i].direction = direction;
    }
}

} // namespace

HandwritingEngine::HandwritingEngine(const GlyphDatabase &database, const HandwritingParams &params)
    : m_database(database)
    , m_params(params)
{
}

double HandwritingEngine::appendGlyph(WritingTrajectory &trajectory, const GlyphVariant &variant,
                                      const GlyphPlacement &placement, double startMs)
{
    const double scale = placement.size * placement.scale;
    const double angle = placement.rotationDegrees * kPi / 180.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double speed = placement.speed > 1e-9 ? placement.speed : 1.0;
    const QPointF origin = placement.origin + QPointF(0.0, placement.baselineOffset);

    // Rotação anti-horária com Y para baixo
    const auto toBoard = [&](const QPointF &p) {
        const double x = p.x() * scale;
        const double y = p.y() * scale;
        return origin + QPointF(x * c + y * s, -x * s + y * c);
    };

    GlyphSpan span;
    span.character = variant.character;
    span.variantId = variant.variantId;
    span.origin = placement.origin;
    span.advance = variant.metrics.bounds.right() * scale;
    span.startMs = startMs;
    const int glyphIndex = int(trajectory.glyphs.size());
    bool emptyBounds = true;
    bool firstStroke = true;

    for (const Stroke &stroke : variant.strokes) {
        if (stroke.points.empty())
            continue;
        const double strokeStart = startMs + stroke.startMs / speed;
        const QPointF firstPos = toBoard(stroke.points.front().pos);

        // Giz levantado desde o último contato (de outra letra ou desta)
        if (!trajectory.segments.empty()) {
            const TrajectorySegment &previous = trajectory.segments.back();
            TrajectorySegment up;
            up.pen = PenState::Up;
            up.from = previous.to;
            up.to = firstPos;
            up.startMs = previous.endMs;
            up.endMs = std::max(strokeStart, previous.endMs);
            up.glyph = previous.glyph == glyphIndex ? glyphIndex : -1;
            trajectory.segments.push_back(up);
        }

        // O glifo começa no primeiro contato: o voo até ele é da letra anterior
        if (firstStroke) {
            span.firstSegment = trajectory.segments.size();
            span.startMs = strokeStart;
            firstStroke = false;
        }

        TrajectorySegment down;
        down.pen = PenState::Down;
        down.first = trajectory.points.size();
        down.count = stroke.points.size();
        down.glyph = glyphIndex;
        down.stroke = stroke.id;
        for (const WritingPoint &p : stroke.points) {
            TrajectoryPoint out;
            out.pos = toBoard(p.pos);
            out.timeMs = strokeStart + p.timeMs / speed;
            out.pressure = p.pressure >= 0.0f ? std::clamp(p.pressure * placement.pressureScale, 0.0f, 1.0f)
                                              : p.pressure;
            out.tilt = p.tilt;
            extend(span.bounds, emptyBounds, out.pos);
            trajectory.points.push_back(out);
        }
        deriveMotion(trajectory.points, down.first, down.count);
        down.from = trajectory.points[down.first].pos;
        down.to = trajectory.points.back().pos;
        down.startMs = trajectory.points[down.first].timeMs;
        down.endMs = trajectory.points.back().timeMs;
        trajectory.segments.push_back(down);
    }

    if (firstStroke)
        span.firstSegment = trajectory.segments.size();
    span.segmentCount = trajectory.segments.size() - span.firstSegment;
    span.endMs = span.segmentCount > 0 ? trajectory.segments.back().endMs : startMs;
    trajectory.glyphs.push_back(span);

    bool emptyTotal = trajectory.glyphs.size() == 1;
    if (!emptyBounds) {
        extend(trajectory.bounds, emptyTotal, span.bounds.topLeft());
        extend(trajectory.bounds, emptyTotal, span.bounds.bottomRight());
    }
    trajectory.durationMs = std::max(trajectory.durationMs, span.endMs);
    return span.advance;
}

WritingTrajectory HandwritingEngine::generate(const QString &text, const HandwritingOptions &options) const
{
    WritingTrajectory trajectory;
    const double speed = options.speed > 1e-9 ? options.speed : 1.0;
    double penX = options.origin.x();
    double clock = 0.0;
    bool afterSpace = false;
    std::uint32_t index = 0;

    for (char32_t code : text.toUcs4()) {
        const QString character = QString::fromUcs4(&code, 1);
        if (character.front().isSpace()) {
            penX += m_params.wordSpacing * options.size;
            afterSpace = true;
            continue;
        }
        const Glyph *glyph = m_database.glyph(character);
        if (!glyph) {
            if (!trajectory.missing.contains(character))
                trajectory.missing += character;
            continue;
        }
        const GlyphVariant &variant = glyph->variants[mix(options.seed, index++) % glyph->variants.size()];

        GlyphPlacement placement;
        placement.origin = QPointF(penX, options.origin.y());
        placement.size = options.size;
        placement.speed = speed;
        if (!trajectory.glyphs.empty())
            clock += (afterSpace ? m_params.wordPenUpMs : m_params.glyphPenUpMs) / speed;
        penX += appendGlyph(trajectory, variant, placement, clock) + m_params.letterSpacing * options.size;
        clock = trajectory.durationMs;
        afterSpace = false;
    }
    return trajectory;
}

} // namespace handwriting
