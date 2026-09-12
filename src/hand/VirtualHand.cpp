#include "VirtualHand.h"

#include "physics/Board.h"
#include "physics/Noise.h"

#include <algorithm>
#include <cmath>

namespace {

double length(const QPointF &p)
{
    return std::hypot(p.x(), p.y());
}

} // namespace

VirtualHand::VirtualHand(Board &board, const HandParams &params, QObject *parent)
    : QObject(parent)
    , m_params(params)
    , m_board(board)
    , m_stroke(board.params(), board.surface, board.deposit, board.chalk)
    , m_eraser(board.params(), board.deposit)
    , m_boardUnits(board.params().boardWidth / params.pixelsPerUnit, board.params().boardHeight / params.pixelsPerUnit)
    , m_handPos(m_boardUnits.width() / 2.0, m_boardUnits.height() / 2.0)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(m_params.tickIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &VirtualHand::tick);
}

void VirtualHand::setParams(const HandParams &params)
{
    const double pixelsPerUnit = m_params.pixelsPerUnit;
    m_params = params;
    m_params.pixelsPerUnit = pixelsPerUnit;
    m_timer.setInterval(m_params.tickIntervalMs);
}

float VirtualHand::basePressure(PressureLevel level) const
{
    return level == PressureLevel::Light  ? m_params.pressureLight
         : level == PressureLevel::Strong ? m_params.pressureStrong
                                          : m_params.pressureNormal;
}

void VirtualHand::draw(const std::vector<Polyline> &strokes, PressureLevel pressure, Motion motion)
{
    const float base = basePressure(pressure);
    const bool writing = motion == Motion::Writing;
    const double speed = writing ? m_params.writingSpeed : m_params.baseSpeed;

    beginJob(Tool::Chalk, writing ? m_params.writingPenLiftMs : m_params.penLiftMs);
    for (const Polyline &stroke : strokes)
        appendStroke(stroke, speed, base, true);
    startPlayback();
}

void VirtualHand::draw(const std::vector<Polyline> &strokes, const std::vector<PressureLevel> &pressures,
                       Motion motion)
{
    const bool writing = motion == Motion::Writing;
    const double speed = writing ? m_params.writingSpeed : m_params.baseSpeed;

    beginJob(Tool::Chalk, writing ? m_params.writingPenLiftMs : m_params.penLiftMs);
    for (std::size_t i = 0; i < strokes.size(); ++i) {
        const PressureLevel level = i < pressures.size() ? pressures[i] : PressureLevel::Normal;
        appendStroke(strokes[i], speed, basePressure(level), true);
    }
    startPlayback();
}

void VirtualHand::erase(const QRectF &area)
{
    beginJob(Tool::Eraser, m_params.penLiftMs);

    // Zigue-zague horizontal cobrindo a área (com margem), dentro da lousa
    const QRectF r = area.adjusted(-m_params.eraserMargin, -m_params.eraserMargin,
                                   m_params.eraserMargin, m_params.eraserMargin)
                         .intersected(QRectF(QPointF(0, 0), m_boardUnits));
    if (!r.isEmpty()) {
        Polyline zigzag;
        const double spacing = m_params.eraserRowSpacing;
        const int rows = std::max(1, static_cast<int>(std::ceil(r.height() / spacing)));
        const double step = r.height() / rows;
        for (int i = 0; i < rows; ++i) {
            const double y = r.top() + step * (i + 0.5);
            const bool leftToRight = i % 2 == 0;
            zigzag.emplace_back(leftToRight ? r.left() : r.right(), y);
            zigzag.emplace_back(leftToRight ? r.right() : r.left(), y);
        }
        appendStroke(zigzag, m_params.eraserSpeed, m_params.pressureNormal, false);
    }
    startPlayback();
}

void VirtualHand::clearBoard()
{
    cancel();
    m_board.clear();
    emit boardChanged();
    m_busy = true;
    finishLater();
}

void VirtualHand::cancel()
{
    ++m_generation;
    m_timer.stop();
    if (m_strokeOpen) {
        if (m_tool == Tool::Chalk)
            m_stroke.end();
        else
            m_eraser.end();
        m_strokeOpen = false;
    }
    m_simTime = m_jobStart + m_playhead;
    m_plan.clear();
    m_next = 0;
    m_busy = false;
}

void VirtualHand::setPaused(bool paused)
{
    m_paused = paused;
    if (!m_busy || m_plan.empty())
        return;
    if (paused) {
        m_timer.stop();
    } else {
        m_clock.restart();
        m_timer.start();
    }
}

void VirtualHand::setSpeed(double factor)
{
    m_speed = factor;
}

void VirtualHand::beginJob(Tool tool, double penLiftMs)
{
    if (m_busy)
        cancel();
    m_tool = tool;
    m_penLift = penLiftMs;
    m_plan.clear();
    m_next = 0;
    m_playhead = 0.0;
    m_cursor = 0.0;
    m_jobStart = m_simTime;
}

void VirtualHand::startPlayback()
{
    m_busy = true;
    if (m_plan.empty()) {
        finishLater();
        return;
    }
    // A mão ainda leva um instante para se levantar depois do último traço
    m_planEnd = m_cursor + m_penLift;
    if (!m_paused) {
        m_clock.start();
        m_timer.start();
    }
}

void VirtualHand::finishLater()
{
    // Termina na próxima volta do event loop (nunca dentro de quem chamou)
    QTimer::singleShot(0, this, [this, generation = m_generation] {
        if (generation != m_generation)
            return;
        m_busy = false;
        emit finished();
    });
}

void VirtualHand::resample(const Polyline &units)
{
    // Pontos a cada sampleSpacing ao longo do comprimento da polilinha
    m_points.clear();
    m_points.push_back(units.front());
    const double spacing = m_params.sampleSpacing;
    double carry = 0.0;
    for (std::size_t i = 1; i < units.size(); ++i) {
        const QPointF a = units[i - 1];
        const QPointF d = units[i] - a;
        const double len = length(d);
        double t = spacing - carry;
        for (; t < len; t += spacing)
            m_points.push_back(a + d * (t / len));
        carry = len - (t - spacing);
    }
    if (length(units.back() - m_points.back()) > 1e-6)
        m_points.push_back(units.back());

    // Traço fechado: a mão passa um pouco do ponto inicial
    const bool closed = units.size() > 2 && length(units.back() - units.front()) < 1e-6;
    if (closed) {
        double covered = 0.0;
        for (std::size_t i = 1; i < units.size() && covered < m_params.closedOvershoot; ++i) {
            const QPointF a = units[i - 1];
            const QPointF d = units[i] - a;
            const double len = length(d);
            for (double t = spacing; t <= len && covered + t <= m_params.closedOvershoot; t += spacing)
                m_points.push_back(a + d * (t / len));
            covered += len;
        }
    }
}

void VirtualHand::appendStroke(const Polyline &units, double speed, float basePressure, bool wobble)
{
    if (units.empty())
        return;
    if (units.size() == 1) { // um ponto isolado vira um toque curtíssimo do giz
        appendStroke({units[0], units[0] + QPointF(0.05, 0.0)}, speed, basePressure, wobble);
        return;
    }
    resample(units);
    const std::size_t n = m_points.size();

    // Tempo no ar até o início do traço
    m_cursor += m_penLift + length(m_points.front() - m_handPos) / m_params.travelSpeed * 1000.0;

    // Distância acumulada
    m_dist.assign(n, 0.0);
    for (std::size_t i = 1; i < n; ++i)
        m_dist[i] = m_dist[i - 1] + length(m_points[i] - m_points[i - 1]);
    const double total = m_dist[n - 1];

    // Perfil de velocidade: limite por curvatura, depois aceleração para frente e para trás
    m_vel.assign(n, speed);
    for (std::size_t i = 1; i + 1 < n; ++i) {
        const QPointF in = m_points[i] - m_points[i - 1];
        const QPointF out = m_points[i + 1] - m_points[i];
        const double li = length(in), lo = length(out);
        if (li <= 0.0 || lo <= 0.0)
            continue;
        const double cosTurn = std::clamp((in.x() * out.x() + in.y() * out.y()) / (li * lo), -1.0, 1.0);
        const double curvature = std::acos(cosTurn) / (0.5 * (li + lo));
        m_vel[i] = std::max(m_params.minCurveSpeed, speed / (1.0 + m_params.curveSlowdown * curvature));
    }
    m_vel[0] = std::min(speed, m_params.startSpeed);
    m_vel[n - 1] = std::min(speed, m_params.endSpeed);
    const double a2 = 2.0 * m_params.acceleration;
    for (std::size_t i = 1; i < n; ++i)
        m_vel[i] = std::min(m_vel[i], std::sqrt(m_vel[i - 1] * m_vel[i - 1] + a2 * (m_dist[i] - m_dist[i - 1])));
    for (std::size_t i = n - 1; i-- > 0;)
        m_vel[i] = std::min(m_vel[i], std::sqrt(m_vel[i + 1] * m_vel[i + 1] + a2 * (m_dist[i + 1] - m_dist[i])));

    // Tremor e variação de pressão determinísticos por traço, com ruído 1D suave
    // (value noise): irregular como a mão, sem a regularidade de uma senoide
    const auto k = static_cast<std::int32_t>(m_strokeCounter++);
    const std::uint32_t seed = m_params.wobbleSeed;
    const auto smoothNoise = [k](double x, std::uint32_t noiseSeed) { // em [-1, 1]
        const double fx = std::floor(x);
        const auto i = static_cast<std::int32_t>(fx);
        double t = x - fx;
        t = t * t * (3.0 - 2.0 * t);
        const double a = noise::value(i, k, noiseSeed), b = noise::value(i + 1, k, noiseSeed);
        return 2.0 * (a + (b - a) * t) - 1.0;
    };
    const double amplitude = m_params.wobbleMin + (m_params.wobbleMax - m_params.wobbleMin) * noise::value(k, 0, seed + 3);
    const double ppu = m_params.pixelsPerUnit;

    double t = m_cursor;
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0)
            t += (m_dist[i] - m_dist[i - 1]) / (0.5 * (m_vel[i] + m_vel[i - 1])) * 1000.0;
        const double s = m_dist[i];

        QPointF pos = m_points[i];
        if (wobble) {
            // Deslocamento perpendicular ao traço (duas oitavas de ruído de baixa frequência)
            const QPointF tangent = m_points[std::min(i + 1, n - 1)] - m_points[i > 0 ? i - 1 : 0];
            const double tl = length(tangent);
            if (tl > 0.0) {
                const double w = amplitude * (0.7 * smoothNoise(s / m_params.wobbleWavelength, seed)
                                            + 0.3 * smoothNoise(s / (0.4 * m_params.wobbleWavelength), seed + 1));
                pos += QPointF(-tangent.y() / tl, tangent.x() / tl) * w;
            }
        }

        // Pressão: sobe no toque, alivia no fim, varia devagar no meio
        double ramp = 1.0;
        if (s < m_params.pressureRampIn)
            ramp = std::min(ramp, m_params.pressureStart + (1.0 - m_params.pressureStart) * s / m_params.pressureRampIn);
        if (total - s < m_params.pressureRampOut)
            ramp = std::min(ramp, m_params.pressureEnd + (1.0 - m_params.pressureEnd) * (total - s) / m_params.pressureRampOut);
        const double variation = 1.0 + m_params.pressureVariation * smoothNoise(s / m_params.pressureWavelength, seed + 2);
        const auto pressure = static_cast<float>(std::clamp(basePressure * ramp * variation, 0.0, 1.0));

        m_plan.push_back({pos * ppu, pressure, t, i == 0, i == n - 1});
    }

    m_cursor = t;
    m_handPos = m_points.back();
}

void VirtualHand::deliver(const TimedSample &sample)
{
    const ChalkSample chalk{sample.pos, sample.pressure, m_params.tilt, m_jobStart + sample.timeMs};
    if (sample.first) {
        m_strokeOpen = true;
        if (m_tool == Tool::Chalk)
            m_stroke.begin(chalk);
        else
            m_eraser.begin(chalk);
    } else if (m_tool == Tool::Chalk) {
        m_stroke.add(chalk);
    } else {
        m_eraser.add(chalk);
    }

    if (sample.last) {
        if (m_tool == Tool::Chalk)
            m_stroke.end();
        else
            m_eraser.end();
        m_strokeOpen = false;
    }
}

void VirtualHand::tick()
{
    m_playhead += static_cast<double>(m_clock.nsecsElapsed()) / 1e6 * m_speed;
    m_clock.restart();

    while (m_next < m_plan.size() && m_plan[m_next].timeMs <= m_playhead)
        deliver(m_plan[m_next++]);
    emit boardChanged();

    if (m_next >= m_plan.size() && m_playhead >= m_planEnd) {
        m_timer.stop();
        m_simTime = m_jobStart + m_planEnd;
        m_plan.clear();
        m_next = 0;
        m_busy = false;
        emit finished();
    }
}
