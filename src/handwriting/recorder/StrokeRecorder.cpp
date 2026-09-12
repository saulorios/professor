#include "StrokeRecorder.h"

#include "handwriting/data/GlyphGeometry.h"

#include <QDateTime>

#include <algorithm>

namespace handwriting {

StrokeRecorder::StrokeRecorder(const HandwritingParams &params)
    : m_params(params)
{
}

void StrokeRecorder::begin(const RawSample &sample)
{
    if (m_open)
        end();
    if (m_strokes.empty()) {
        m_glyphStart = sample.timeMs;
        m_capture.pointer = sample.pointer;
        m_capture.unitPx = m_guide.unitPx;
        m_capture.originPx = QPointF(0.0, m_guide.baselineY);
        m_capture.areaPx = m_guide.areaPx;
        m_capture.recordedAt = QDateTime::currentDateTimeUtc();
    }
    // Relógio crescente: um toque nunca começa antes do fim do stroke anterior
    const double previousEnd = m_strokes.empty() ? m_glyphStart
                                                 : m_glyphStart + m_strokes.back().startMs
                                                       + m_strokes.back().rawPoints.back().timeMs;
    m_strokeStart = std::max(sample.timeMs, previousEnd);

    Stroke stroke;
    stroke.id = int(m_strokes.size());
    stroke.startMs = m_strokeStart - m_glyphStart;
    m_strokes.push_back(std::move(stroke));
    m_open = true;
    append(sample);
}

void StrokeRecorder::add(const RawSample &sample)
{
    if (m_open)
        append(sample);
}

void StrokeRecorder::end(const RawSample &sample)
{
    if (!m_open)
        return;
    // O pointerUp só entra se trouxer posição nova (costuma repetir o último move)
    const WritingPoint &last = m_strokes.back().rawPoints.back();
    if (sample.pos != last.pos)
        append(sample);
    end();
}

void StrokeRecorder::end()
{
    if (!m_open)
        return;
    m_open = false;
    geometry::computeVelocities(m_strokes.back().points);
    geometry::computeVelocities(m_strokes.back().rawPoints);
    m_strokes.back().metrics = geometry::strokeMetrics(m_strokes.back().points, m_params);
}

bool StrokeRecorder::undo()
{
    if (m_open)
        end();
    if (m_strokes.empty())
        return false;
    m_strokes.pop_back();
    return true;
}

void StrokeRecorder::clear()
{
    m_strokes.clear();
    m_open = false;
}

GlyphVariant StrokeRecorder::build(const QString &character, const QString &variantId) const
{
    GlyphVariant variant;
    variant.character = character;
    variant.variantId = variantId;
    variant.capture = m_capture;
    variant.strokes = m_strokes;
    geometry::normalizeLeftEdge(variant, m_params);
    return variant;
}

void StrokeRecorder::append(const RawSample &sample)
{
    Stroke &stroke = m_strokes.back();
    double t = sample.timeMs - m_strokeStart;
    if (!stroke.rawPoints.empty())
        t = std::max(t, stroke.rawPoints.back().timeMs);
    t = std::max(t, 0.0);

    WritingPoint raw;
    raw.pos = sample.pos;
    raw.timeMs = t;
    raw.pressure = sample.pressure;
    raw.tilt = sample.tilt;
    stroke.rawPoints.push_back(raw);

    // Mesma amostra no espaço do glifo, com a guia do primeiro toque
    WritingPoint point = raw;
    point.pos = (sample.pos - m_capture.originPx) / m_capture.unitPx;
    stroke.points.push_back(point);
}

} // namespace handwriting
