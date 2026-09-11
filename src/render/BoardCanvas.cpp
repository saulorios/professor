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

    // Atalho de teste: limpa a lousa
    auto *clearShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete), this);
    connect(clearShortcut, &QShortcut::activated, this, &BoardCanvas::clear);
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

void BoardCanvas::paintEvent(QPaintEvent *)
{
    // Só a região pedida pelo update() é redesenhada (o QPainter já vem recortado)
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(boardRect(), m_image);
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
    if (tool == Tool::Chalk)
        m_stroke.begin(sample);
    else
        m_eraser.begin(sample);
    refresh();
}

void BoardCanvas::moveTool(const ChalkSample &sample)
{
    if (m_tool == Tool::Chalk)
        m_stroke.add(sample);
    else
        m_eraser.add(sample);
    refresh();
}

void BoardCanvas::endTool()
{
    if (m_tool == Tool::Chalk)
        m_stroke.end();
    else
        m_eraser.end();
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

void BoardCanvas::updateCaptionGeometry()
{
    if (!m_caption || !m_caption->isVisible())
        return;
    // Faixa na largura da lousa, encostada na borda inferior dela
    const QRect board = boardRect().toRect();
    const int h = m_caption->heightForWidth(board.width());
    m_caption->setGeometry(board.left(), board.bottom() + 1 - h, board.width(), h);
}
