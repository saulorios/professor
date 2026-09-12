#include "BoardCanvas.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QTabletEvent>

#include <algorithm>
#include <cmath>

namespace {

// Interpolação linear de um canal de cor: a + (b - a) * t
int mixChannel(int a, int b, float t)
{
    return a + static_cast<int>(static_cast<float>(b - a) * t + 0.5f);
}

// Canal de cor multiplicado por k, limitado a 0..255
int scaleChannel(int value, float k)
{
    return std::clamp(static_cast<int>(std::lround(static_cast<float>(value) * k)), 0, 255);
}

} // namespace

BoardCanvas::BoardCanvas(Board &board, QWidget *parent)
    : QWidget(parent)
    , m_board(board)
    , m_stroke(board.params(), board.surface, board.deposit, board.chalk)
    , m_eraser(board.params(), board.deposit)
{
    // O botão direito é o apagador: sem menu de contexto
    setContextMenuPolicy(Qt::PreventContextMenu);

    buildBaseImage();
    m_image = m_base.copy();
    m_board.deposit.markDirty(m_board.deposit.bounds()); // compõe o que já estiver no depósito
    refresh();

    // Legenda (estilo no QSS, #Caption); não bloqueia o desenho por baixo dela
    m_caption = new QLabel(this);
    m_caption->setObjectName("Caption");
    m_caption->setAlignment(Qt::AlignCenter);
    m_caption->setWordWrap(true);
    m_caption->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_caption->hide();

    // Indicador de gravação (estilo no QSS, #RecordingBadge)
    m_recording = new QLabel("● Gravando", this);
    m_recording->setObjectName("RecordingBadge");
    m_recording->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_recording->hide();

    // Atalho de teste: limpa a lousa
    auto *clearShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete), this);
    connect(clearShortcut, &QShortcut::activated, this, &BoardCanvas::clear);

    // Fade do giz ao pausar ou terminar
    m_fadeTimer.setInterval(16);
    connect(&m_fadeTimer, &QTimer::timeout, this, [this] {
        if (m_fadeClock.elapsed() >= qint64(m_giz.fadeMs)) {
            m_fadeTimer.stop();
            m_fading = false;
            m_chalkVisible = false;
        }
        refreshChalk(m_chalkPose, true);
    });
}

void BoardCanvas::setRecording(bool on)
{
    m_recording->setVisible(on);
    updateCaptionGeometry();
}

void BoardCanvas::setGizParams(const GizParams &params)
{
    const bool had = m_chalkVisible;
    const ChalkPose previous = m_chalkPose;
    m_giz = params;
    refreshChalk(previous, had);
}

void BoardCanvas::setChalkPose(const ChalkPose &pose)
{
    const bool had = m_chalkVisible;
    const ChalkPose previous = m_chalkPose;
    m_chalkPose = pose;
    m_chalkVisible = true;
    m_fading = false;
    m_fadeTimer.stop();
    refreshChalk(previous, had);
}

void BoardCanvas::hideChalk()
{
    if (!m_chalkVisible || m_fading)
        return;
    m_fading = true;
    m_fadeClock.start();
    m_fadeTimer.start();
}

void BoardCanvas::clear()
{
    m_board.clear();
    refresh();
}

void BoardCanvas::rebuildSurface()
{
    buildBaseImage();
    m_board.deposit.markDirty(m_board.deposit.bounds());
    refresh();
}

void BoardCanvas::setCaption(const QString &text)
{
    m_caption->setText(text);
    m_caption->setVisible(!text.isEmpty());
    updateCaptionGeometry();
}

void BoardCanvas::setCaptionBand(double boardPixels)
{
    m_captionBandPx = boardPixels;
    updateCaptionGeometry();
}

void BoardCanvas::setOverlay(const std::vector<OverlayBox> &boxes)
{
    m_overlay = boxes;
    if (m_overlayVisible)
        update();
}

void BoardCanvas::setOverlayGeometry(const std::vector<OverlayLine> &lines, const std::vector<QPointF> &points)
{
    m_overlayLines = lines;
    m_overlayPoints = points;
    if (m_overlayVisible)
        update();
}

void BoardCanvas::setOverlayVisible(bool visible)
{
    m_overlayVisible = visible;
    update();
}

void BoardCanvas::paintEvent(QPaintEvent *)
{
    // Só a região pedida pelo update() é redesenhada (o QPainter já vem recortado)
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF board = boardRect();
    painter.drawImage(board, m_image);

    // Giz da mão virtual, por cima da lousa e fora do DepositBuffer
    if (m_chalkVisible && m_giz.visible) {
        const double fade = m_fading
                                ? std::clamp(1.0 - double(m_fadeClock.elapsed()) / std::max(m_giz.fadeMs, 1.0), 0.0, 1.0)
                                : 1.0;
        painter.save();
        painter.translate(board.topLeft());
        const qreal s = board.width() / m_board.deposit.width();
        painter.scale(s, s);
        paintChalk(painter, m_chalkPose, m_giz, fade);
        painter.restore();
    }

    if (!m_overlayVisible)
        return;

    // Modo de depuração: bounding boxes e ids por cima da lousa (fora do DepositBuffer)
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPixelSize(m_params.debugFontPx);
    painter.setFont(font);
    const qreal scale = board.width() / m_board.deposit.width();
    for (const OverlayBox &box : m_overlay) {
        const QRectF r(board.topLeft() + box.rect.topLeft() * scale, box.rect.size() * scale);
        QPen pen(box.area ? m_params.debugAreaColor : m_params.debugBoxColor);
        pen.setCosmetic(true);
        pen.setStyle(box.area ? Qt::DashLine : Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r);
        if (!box.label.isEmpty())
            painter.drawText(r.topLeft() + QPointF(2.0, -3.0), box.label);
    }

    // Objetos 3D: arestas ocultas (mesmo quando não são desenhadas) e fuga
    for (const OverlayLine &line : m_overlayLines) {
        QPen pen(line.vanishing ? m_params.debugVanishingColor : m_params.debugHiddenColor);
        pen.setCosmetic(true);
        pen.setStyle(line.vanishing ? Qt::DotLine : Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(QLineF(board.topLeft() + line.line.p1() * scale,
                                board.topLeft() + line.line.p2() * scale));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_params.debugVanishingColor);
    for (const QPointF &point : m_overlayPoints)
        painter.drawEllipse(board.topLeft() + point * scale, m_params.debugPointRadius, m_params.debugPointRadius);
}

void BoardCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateCaptionGeometry();
}

void BoardCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_tool != Tool::None)
        return;
    if (event->button() == Qt::LeftButton)
        beginTool(Tool::Chalk, mouseSample(event));
    else if (event->button() == Qt::RightButton)
        beginTool(Tool::Eraser, mouseSample(event));
}

void BoardCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_tool != Tool::None)
        moveTool(mouseSample(event));
}

void BoardCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    const Qt::MouseButton toolButton = m_tool == Tool::Chalk ? Qt::LeftButton : Qt::RightButton;
    if (m_tool != Tool::None && event->button() == toolButton)
        endTool();
}

void BoardCanvas::tabletEvent(QTabletEvent *event)
{
    // Mesa digitalizadora: pressão e inclinação reais
    const ChalkSample sample{toBoard(event->position()), static_cast<float>(event->pressure()),
                             tabletTilt(event), static_cast<double>(event->timestamp())};

    switch (event->type()) {
    case QEvent::TabletPress:
        if (m_tool == Tool::None)
            beginTool((event->buttons() & Qt::RightButton) ? Tool::Eraser : Tool::Chalk, sample);
        break;
    case QEvent::TabletMove:
        if (m_tool != Tool::None)
            moveTool(sample);
        break;
    case QEvent::TabletRelease:
        if (m_tool != Tool::None)
            endTool();
        break;
    default:
        break;
    }

    // Aceitar impede que o Qt gere também eventos de mouse a partir da caneta
    event->accept();
}

QRectF BoardCanvas::boardRect() const
{
    const qreal boardWidth = m_board.deposit.width();
    const qreal boardHeight = m_board.deposit.height();
    const qreal scale = std::min(width() / boardWidth, height() / boardHeight);
    const QSizeF size(boardWidth * scale, boardHeight * scale);
    return QRectF(QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0), size);
}

QPointF BoardCanvas::toBoard(const QPointF &widgetPos) const
{
    const QRectF board = boardRect();
    const qreal scale = m_board.deposit.width() / board.width();
    return (widgetPos - board.topLeft()) * scale;
}

QRect BoardCanvas::toWidget(const QRect &boardArea) const
{
    const QRectF board = boardRect();
    const qreal scale = board.width() / m_board.deposit.width();
    const QRectF area(board.x() + boardArea.x() * scale, board.y() + boardArea.y() * scale,
                      boardArea.width() * scale, boardArea.height() * scale);
    // Margem de 1px para a suavização da escala
    return area.toAlignedRect().adjusted(-1, -1, 1, 1);
}

ChalkSample BoardCanvas::mouseSample(const QMouseEvent *event) const
{
    // Mouse: pressão fixa e giz de ponta
    return {toBoard(event->position()), m_params.mousePressure, m_params.mouseTilt,
            static_cast<double>(event->timestamp())};
}

float BoardCanvas::tabletTilt(const QTabletEvent *event) const
{
    const auto angle = static_cast<float>(std::hypot(event->xTilt(), event->yTilt()));
    const float t = (angle - m_params.tiltStartDegrees)
                  / (m_params.tiltFullDegrees - m_params.tiltStartDegrees);
    return std::clamp(t, 0.0f, 1.0f);
}

void BoardCanvas::beginTool(Tool tool, const ChalkSample &sample)
{
    m_tool = tool;
    if (tool == Tool::Chalk) {
        m_stroke.begin(sample);
        emit freeStrokeStarted();
        emit freeSample(sample.pos, sample.pressure, sample.timeMs);
    } else {
        m_eraser.begin(sample);
    }
    refresh();
}

void BoardCanvas::moveTool(const ChalkSample &sample)
{
    if (m_tool == Tool::Chalk) {
        m_stroke.add(sample);
        emit freeSample(sample.pos, sample.pressure, sample.timeMs);
    } else {
        m_eraser.add(sample);
    }
    refresh();
}

void BoardCanvas::endTool()
{
    if (m_tool == Tool::Chalk) {
        m_stroke.end();
        emit freeStrokeFinished();
    } else {
        m_eraser.end();
    }
    m_tool = Tool::None;
}

void BoardCanvas::buildBaseImage()
{
    const int w = m_board.surface.width();
    const int h = m_board.surface.height();
    const QColor &color = m_params.boardColor;

    m_base = QImage(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(m_base.scanLine(y));
        for (int x = 0; x < w; ++x) {
            // Variação sutil de brilho conforme o relevo da superfície
            const float k = 1.0f + m_params.boardVariation * (2.0f * m_board.surface.heightAt(x, y) - 1.0f);
            line[x] = qRgb(scaleChannel(color.red(), k), scaleChannel(color.green(), k),
                           scaleChannel(color.blue(), k));
        }
    }
}

void BoardCanvas::refresh()
{
    const QRect dirty = m_board.deposit.takeDirty();
    if (dirty.isEmpty())
        return;

    const QRgb chalk = m_params.chalkColor.rgb();
    const int chalkR = qRed(chalk);
    const int chalkG = qGreen(chalk);
    const int chalkB = qBlue(chalk);

    for (int y = dirty.top(); y <= dirty.bottom(); ++y) {
        const auto *base = reinterpret_cast<const QRgb *>(m_base.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(m_image.scanLine(y));
        const float *deposit = m_board.deposit.data() + m_board.deposit.index(0, y);

        for (int x = dirty.left(); x <= dirty.right(); ++x) {
            // cor = mix(corLousa, corGiz, depósito)
            const float t = deposit[x];
            const QRgb b = base[x];
            out[x] = qRgb(mixChannel(qRed(b), chalkR, t), mixChannel(qGreen(b), chalkG, t),
                          mixChannel(qBlue(b), chalkB, t));
        }
    }

    update(toWidget(dirty));
}

void BoardCanvas::refreshChalk(const ChalkPose &previous, bool hadChalk)
{
    // Só a região do giz (a antiga e a nova) precisa ser repintada
    QRect region;
    if (hadChalk)
        region = toWidget(chalkBounds(previous, m_giz).toAlignedRect());
    if (m_chalkVisible)
        region = region.united(toWidget(chalkBounds(m_chalkPose, m_giz).toAlignedRect()));
    if (!region.isEmpty())
        update(region);
}

void BoardCanvas::updateCaptionGeometry()
{
    if (m_recording && m_recording->isVisible()) {
        // Canto superior esquerdo da lousa, discreto
        const QRect board = boardRect().toRect();
        m_recording->adjustSize();
        m_recording->move(board.left() + 12, board.top() + 12);
    }
    if (!m_caption || !m_caption->isVisible())
        return;
    // Faixa na largura da lousa, encostada na borda inferior dela, com pelo menos
    // a altura reservada à legenda (a cena não desenha nessa faixa)
    const QRectF boardF = boardRect();
    const QRect board = boardF.toRect();
    const qreal scale = boardF.width() / m_board.deposit.width();
    const int band = static_cast<int>(std::ceil(m_captionBandPx * scale));
    const int h = std::max(band, m_caption->heightForWidth(board.width()));
    m_caption->setGeometry(board.left(), board.bottom() + 1 - h, board.width(), h);
}
