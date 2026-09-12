#include "LessonRecorder.h"

#include <QJsonArray>

#include <cmath>
#include <vector>

namespace {

// Distância do ponto p à reta que passa por a e b
double distanceToLine(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF d = b - a;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-12)
        return std::hypot(p.x() - a.x(), p.y() - a.y());
    return std::abs(d.x() * (a.y() - p.y()) - (a.x() - p.x()) * d.y()) / len;
}

// Douglas-Peucker: marca quais pontos ficam
void simplify(const std::vector<QPointF> &points, std::size_t first, std::size_t last, double tolerance,
              std::vector<bool> &keep)
{
    if (last <= first + 1)
        return;
    double worst = 0.0;
    std::size_t index = first;
    for (std::size_t i = first + 1; i < last; ++i) {
        const double d = distanceToLine(points[i], points[first], points[last]);
        if (d > worst) {
            worst = d;
            index = i;
        }
    }
    if (worst <= tolerance)
        return;
    keep[index] = true;
    simplify(points, first, index, tolerance, keep);
    simplify(points, index, last, tolerance, keep);
}

} // namespace

LessonRecorder::LessonRecorder(QObject *parent)
    : QObject(parent)
{
}

void LessonRecorder::setRecording(bool on)
{
    if (on == m_recording)
        return;
    m_recording = on;
    if (on)
        m_count = 0;
    else
        endStroke();
    emit recordingChanged(m_recording);
}

void LessonRecorder::beginStroke()
{
    if (!m_recording)
        return;
    m_inStroke = true;
    m_points.clear();
    m_pressure.clear();
    m_time.clear();
    m_start = 0.0;
}

void LessonRecorder::addSample(const QPointF &canvasPixels, float pressure, double timeMs)
{
    if (!m_recording || !m_inStroke)
        return;
    if (m_points.empty())
        m_start = timeMs;
    m_points.push_back(canvasPixels / m_params.pixelsPerUnit); // já em coordenadas do canvas
    m_pressure.push_back(pressure);
    m_time.push_back(timeMs - m_start);
}

void LessonRecorder::endStroke()
{
    if (!m_inStroke)
        return;
    m_inStroke = false;
    if (m_points.size() < 2)
        return;

    // Só os pontos que mudam o desenho mais que a tolerância entram no arquivo
    std::vector<bool> keep(m_points.size(), false);
    keep.front() = keep.back() = true;
    simplify(m_points, 0, m_points.size() - 1, m_params.tolerance, keep);

    QJsonArray points;
    for (std::size_t i = 0; i < m_points.size(); ++i) {
        if (!keep[i])
            continue;
        points.append(QJsonArray{std::round(m_points[i].x() * 100.0) / 100.0,
                                 std::round(m_points[i].y() * 100.0) / 100.0,
                                 std::round(double(m_pressure[i]) * 100.0) / 100.0,
                                 std::round(m_time[i]) / 1000.0});
    }
    emit commandRecorded(QJsonObject{{"tipo", "traco_livre"},
                                     {"id", QString("traco_%1").arg(++m_count)},
                                     {"pontos", points}});
}
