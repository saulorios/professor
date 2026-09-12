#include "HandwritingCapture.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTouchEvent>

#include <cmath>

using namespace handwriting;

namespace {

constexpr double kPi = 3.141592653589793;

// Uma cor por stroke, para a ordem ficar visível
QColor strokeColor(int index, const QColor &ink, bool colored)
{
    if (!colored)
        return ink;
    static const QColor palette[] = {QColor(0xF2, 0xF0, 0xE6), QColor(0x9C, 0xDC, 0xFE), QColor(0xCE, 0x91, 0x78),
                                     QColor(0xB5, 0xCE, 0xA8), QColor(0xC5, 0x86, 0xC0), QColor(0xDC, 0xDC, 0xAA)};
    return palette[index % int(sizeof(palette) / sizeof(palette[0]))];
}

void drawArrow(QPainter &painter, const QPointF &tip, double angle, double size)
{
    const QPointF back(std::cos(angle), std::sin(angle));
    const QPointF side(-back.y(), back.x());
    QPainterPath head;
    head.moveTo(tip);
    head.lineTo(tip - back * size + side * size * 0.45);
    head.lineTo(tip - back * size - side * size * 0.45);
    head.closeSubpath();
    painter.fillPath(head, painter.pen().color());
}

} // namespace

HandwritingCapture::HandwritingCapture(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(360, 300);
    setCursor(Qt::CrossCursor);
    updateGuide();
}

double HandwritingCapture::baselineY() const
{
    return height() * m_params.baseline;
}

double HandwritingCapture::unitPx() const
{
    return height() * m_params.guideHeight;
}

void HandwritingCapture::updateGuide()
{
    CaptureGuide guide;
    guide.baselineY = baselineY();
    guide.unitPx = unitPx();
    guide.areaPx = QSizeF(size());
    m_recorder.setGuide(guide);
}

void HandwritingCapture::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateGuide();
}

void HandwritingCapture::setCharacter(const QString &character)
{
    m_character = character;
    update();
}

void HandwritingCapture::showVariant(const GlyphVariant &variant)
{
    m_recorder.clear();
    m_variant = variant;
    update();
    emit strokesChanged();
}

void HandwritingCapture::clear()
{
    m_recorder.clear();
    m_variant.reset();
    update();
    emit strokesChanged();
}

bool HandwritingCapture::undo()
{
    const bool changed = m_recorder.undo();
    update();
    emit strokesChanged();
    return changed;
}

void HandwritingCapture::setShowTrajectory(bool on)
{
    m_showTrajectory = on;
    update();
}

void HandwritingCapture::setShowPoints(bool on)
{
    m_showPoints = on;
    update();
}

bool HandwritingCapture::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TabletPress:
        handlePointer(static_cast<QPointerEvent *>(event), Phase::Down, PointerKind::Pen);
        return true;
    case QEvent::TabletMove:
        handlePointer(static_cast<QPointerEvent *>(event), Phase::Move, PointerKind::Pen);
        return true;
    case QEvent::TabletRelease:
        handlePointer(static_cast<QPointerEvent *>(event), Phase::Up, PointerKind::Pen);
        return true;
    case QEvent::TouchBegin:
        handlePointer(static_cast<QPointerEvent *>(event), Phase::Down, PointerKind::Touch);
        return true;
    case QEvent::TouchUpdate:
        handlePointer(static_cast<QPointerEvent *>(event), Phase::Move, PointerKind::Touch);
        return true;
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        handlePointer(static_cast<QPointerEvent *>(event), Phase::Up, PointerKind::Touch);
        return true;
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        // Mouse sintetizado a partir de caneta ou toque já foi tratado acima
        if (mouse->pointingDevice() && mouse->pointingDevice()->type() != QInputDevice::DeviceType::Mouse)
            return true;
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton)
            handlePointer(mouse, Phase::Down, PointerKind::Mouse);
        else if (event->type() == QEvent::MouseMove && (mouse->buttons() & Qt::LeftButton))
            handlePointer(mouse, Phase::Move, PointerKind::Mouse);
        else if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton)
            handlePointer(mouse, Phase::Up, PointerKind::Mouse);
        return true;
    }
    default:
        return QWidget::event(event);
    }
}

void HandwritingCapture::handlePointer(QPointerEvent *event, Phase phase, PointerKind kind)
{
    event->accept();
    if (event->points().isEmpty())
        return;
    const QEventPoint &point = event->points().front();

    RawSample sample;
    sample.pos = point.position();
    sample.timeMs = eventTime(event);
    sample.pointer = kind;
    const QPointingDevice *device = event->pointingDevice();
    const bool hasPressure = kind != PointerKind::Mouse && device
                             && device->capabilities().testFlag(QInputDevice::Capability::Pressure);
    if (hasPressure)
        sample.pressure = float(point.pressure());
    if (kind == PointerKind::Pen) {
        const auto *tablet = static_cast<QTabletEvent *>(event);
        if (device && device->capabilities().testFlag(QInputDevice::Capability::XTilt)) {
            const double tx = std::tan(tablet->xTilt() * kPi / 180.0);
            const double ty = std::tan(tablet->yTilt() * kPi / 180.0);
            sample.tilt = float(std::atan(std::hypot(tx, ty)) * 180.0 / kPi);
        }
    }

    switch (phase) {
    case Phase::Down:
        if (m_variant) {
            m_variant.reset();
            emit captureStarted();
        }
        updateGuide();
        m_recorder.begin(sample);
        break;
    case Phase::Move:
        m_recorder.add(sample);
        break;
    case Phase::Up:
        if (!m_recorder.isStrokeOpen())
            return;
        m_recorder.end(sample);
        emit strokesChanged();
        break;
    }
    update();
}

// Instante do evento, em ms. Eventos da plataforma trazem o timestamp da
// entrada (sem o atraso de processamento); os sintéticos vêm com 0 e seguem a
// mesma linha do tempo a partir do último instante, pelo relógio local.
double HandwritingCapture::eventTime(const QPointerEvent *event)
{
    const double time = event->timestamp() != 0
                            ? double(event->timestamp())
                            : m_lastTimeMs + (m_sinceLast.isValid() ? m_sinceLast.nsecsElapsed() / 1e6 : 0.0);
    m_lastTimeMs = time;
    m_sinceLast.start();
    return time;
}

void HandwritingCapture::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), m_params.background);

    const double base = baselineY();
    const double unit = unitPx();

    // Letra-guia ao fundo, só como referência de tamanho
    if (!m_character.isEmpty()) {
        QFont font = painter.font();
        font.setPixelSize(std::max(1, int(unit * m_params.previewLetter / 0.72)));
        painter.setFont(font);
        painter.setPen(m_params.ghost);
        painter.drawText(QRectF(0, base - unit * 1.6, width(), unit * 1.6), Qt::AlignHCenter | Qt::AlignBottom,
                         m_character);
    }

    // Guias: maiúsculas, minúsculas (tracejada), linha de base, descendentes (tracejada)
    QPen guide(m_params.guide, 1.0);
    painter.setPen(guide);
    painter.drawLine(QPointF(0, base - unit), QPointF(width(), base - unit));
    painter.drawLine(QPointF(0, base), QPointF(width(), base));
    guide.setStyle(Qt::DashLine);
    painter.setPen(guide);
    painter.drawLine(QPointF(0, base - unit * m_params.xHeight), QPointF(width(), base - unit * m_params.xHeight));
    painter.drawLine(QPointF(0, base + unit * m_params.descender), QPointF(width(), base + unit * m_params.descender));

    // Captura em andamento: x = 0 é a borda do widget; variante do banco: centralizada
    const std::vector<Stroke> &strokes = m_variant ? m_variant->strokes : m_recorder.strokes();
    const double offsetX = m_variant ? (width() - m_variant->metrics.bounds.width() * unit) / 2.0 : 0.0;
    const auto map = [&](const QPointF &p) { return QPointF(offsetX + p.x() * unit, base + p.y() * unit); };

    // Giz levantado entre strokes
    if (m_showTrajectory) {
        QPen up(m_params.penUp, 1.2, Qt::DotLine);
        painter.setPen(up);
        for (std::size_t i = 1; i < strokes.size(); ++i) {
            if (strokes[i - 1].points.empty() || strokes[i].points.empty())
                continue;
            const QPointF from = map(strokes[i - 1].points.back().pos);
            const QPointF to = map(strokes[i].points.front().pos);
            painter.drawLine(from, to);
            const QPointF d = to - from;
            if (std::hypot(d.x(), d.y()) > m_params.arrowSize * 2.0)
                drawArrow(painter, (from + to) / 2.0, std::atan2(d.y(), d.x()), m_params.arrowSize * 0.8);
        }
    }

    for (std::size_t i = 0; i < strokes.size(); ++i) {
        const Stroke &stroke = strokes[i];
        if (stroke.points.empty())
            continue;
        const QColor color = strokeColor(int(i), m_params.ink, m_showTrajectory);
        QPen pen(color, m_params.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        QPainterPath path(map(stroke.points.front().pos));
        for (std::size_t k = 1; k < stroke.points.size(); ++k)
            path.lineTo(map(stroke.points[k].pos));
        if (stroke.points.size() == 1)
            painter.drawPoint(path.currentPosition());
        else
            painter.drawPath(path);

        if (m_showPoints) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_params.points);
            for (const WritingPoint &p : stroke.points)
                painter.drawEllipse(map(p.pos), m_params.pointRadius, m_params.pointRadius);
            painter.setBrush(Qt::NoBrush);
        }

        if (m_showTrajectory) {
            // Número do stroke no início e seta no fim, na direção do movimento
            const QPointF start = map(stroke.points.front().pos);
            painter.setPen(QPen(color, 1.5));
            painter.drawEllipse(start, m_params.pointRadius * 2.0, m_params.pointRadius * 2.0);
            QFont font = painter.font();
            font.setPixelSize(13);
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(start + QPointF(-m_params.labelOffset * 1.4, -m_params.labelOffset * 0.6),
                             QString::number(i + 1));
            if (stroke.points.size() > 1) {
                std::size_t k = stroke.points.size() - 1;
                const QPointF tip = map(stroke.points[k].pos);
                QPointF from = tip;
                while (k > 0 && std::hypot(from.x() - tip.x(), from.y() - tip.y()) < m_params.arrowSize)
                    from = map(stroke.points[--k].pos);
                if (from != tip)
                    drawArrow(painter, tip, std::atan2(tip.y() - from.y(), tip.x() - from.x()), m_params.arrowSize);
            }
        }
    }
}
