#include "BoardCanvas.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QTabletEvent>
#include <QWheelEvent>

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

// Suaviza o começo e o fim da rolagem automática
double easeInOut(double t)
{
    return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;
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
    setFocusPolicy(Qt::StrongFocus); // Page Up/Down, Home e End

    m_view = QImage(m_board.deposit.width(), m_board.screenHeight(), QImage::Format_RGB32);
    m_board.deposit.markDirty(m_board.deposit.bounds());
    rebuildView();

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

    // Em que parte da lousa estamos ("2/3")
    m_position = new QLabel(this);
    m_position->setObjectName("PositionBadge");
    m_position->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_position->hide();

    // Aviso clicável quando a escrita continua fora da vista
    m_below = new QPushButton("continuando abaixo ↓", this);
    m_below->setObjectName("BelowNotice");
    m_below->setCursor(Qt::PointingHandCursor);
    m_below->setFocusPolicy(Qt::NoFocus);
    m_below->hide();
    connect(m_below, &QPushButton::clicked, this, [this] {
        m_userScrolled = false;
        m_below->hide();
        if (!m_pending.isNull())
            followTo(m_pending);
    });

    // Barra de rolagem fina, encostada na direita da lousa
    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setObjectName("BoardScroll");
    m_scrollBar->setFocusPolicy(Qt::NoFocus);
    m_scrollBar->setSingleStep(int(m_params.wheelStep));
    m_scrollBar->setPageStep(m_board.screenHeight());
    connect(m_scrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        if (std::abs(value - m_scroll) > 0.5)
            applyScroll(value, true);
    });

    // Atalho de teste: limpa a lousa
    auto *clearShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete), this);
    connect(clearShortcut, &QShortcut::activated, this, &BoardCanvas::clear);

    // Animação da rolagem automática
    m_scrollTimer.setInterval(16);
    connect(&m_scrollTimer, &QTimer::timeout, this, [this] {
        const double t = m_scrollDuration > 0
                             ? std::clamp(double(m_scrollClock.elapsed()) / m_scrollDuration, 0.0, 1.0)
                             : 1.0;
        applyScroll(m_scrollFrom + (m_scrollTo - m_scrollFrom) * easeInOut(t), false);
        if (t >= 1.0)
            m_scrollTimer.stop();
    });

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
    updateChrome();
}

void BoardCanvas::clear()
{
    // Só o pó: o tamanho do canvas é decidido pela aula (LessonPlayer)
    m_board.clear();
    refresh();
}

void BoardCanvas::resumeFollowing()
{
    m_userScrolled = false;
    m_pending = QRectF();
    m_below->hide();
}

void BoardCanvas::canvasChanged()
{
    if (m_scroll > maxScroll()) {
        m_scrollTimer.stop();
        m_scroll = maxScroll();
        m_userScrolled = false;
        m_pending = QRectF();
    }
    rebuildView();
    updateChrome();
    update();
}

void BoardCanvas::rebuildSurface()
{
    m_board.deposit.markDirty(m_board.deposit.bounds());
    rebuildView();
    update();
}

void BoardCanvas::setCaption(const QString &text)
{
    m_caption->setText(text);
    m_caption->setVisible(!text.isEmpty());
    updateCaptionGeometry();
}

void BoardCanvas::setCaptionBand(double screenPixels)
{
    m_captionBandPx = screenPixels;
    updateCaptionGeometry();
}

void BoardCanvas::setRecording(bool on)
{
    m_recording->setVisible(on);
    updateChrome();
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

// --- Rolagem ---------------------------------------------------------------

int BoardCanvas::screenCount() const
{
    return std::max(1, m_board.canvasHeight() / m_board.screenHeight());
}

int BoardCanvas::currentScreen() const
{
    return int(std::round(m_scroll / m_board.screenHeight()));
}

double BoardCanvas::maxScroll() const
{
    return std::max(0, m_board.canvasHeight() - m_board.screenHeight());
}

void BoardCanvas::setScroll(double canvasPixels)
{
    m_scrollTimer.stop();
    applyScroll(canvasPixels, true);
}

void BoardCanvas::scrollBy(double canvasPixels)
{
    setScroll(m_scroll + canvasPixels);
}

void BoardCanvas::scrollToScreen(int screen, bool animated)
{
    const double target = std::clamp(double(screen) * m_board.screenHeight(), 0.0, maxScroll());
    if (animated)
        animateTo(target);
    else
        setScroll(target);
}

void BoardCanvas::followTo(const QRectF &canvasRect)
{
    // Uma folga em volta: o elemento não fica colado na borda da tela
    const QRectF area = canvasRect.adjusted(0.0, -m_params.revealMargin, 0.0, m_params.revealMargin);
    m_pending = area;
    const double screen = m_board.screenHeight();
    // Já está à vista? Então não há nada a fazer
    if (area.top() >= m_scroll - 0.5 && area.bottom() <= m_scroll + screen + 0.5) {
        m_below->hide();
        return;
    }
    if (m_userScrolled) {
        // O usuário assumiu a rolagem: avisa, mas não arrasta a vista
        m_below->setText(area.top() < m_scroll ? "continuando acima ↑" : "continuando abaixo ↓");
        m_below->show();
        updateChrome();
        return;
    }
    // Mostra a faixa pedida, encostando o topo dela no topo da tela quando não cabe
    const double target = area.height() >= screen || area.top() < m_scroll ? area.top()
                                                                           : area.bottom() - screen;
    animateTo(std::clamp(target, 0.0, maxScroll()));
}

void BoardCanvas::animateTo(double canvasPixels)
{
    const double target = std::clamp(canvasPixels, 0.0, maxScroll());
    if (std::abs(target - m_scroll) < 0.5) {
        m_scrollTimer.stop();
        return;
    }
    // 600 ms por tela, como um professor girando uma lousa de rolo
    m_scrollFrom = m_scroll;
    m_scrollTo = target;
    m_scrollDuration = std::max(80, int(m_params.scrollAnimationMs * std::abs(target - m_scroll)
                                        / m_board.screenHeight()));
    m_scrollClock.start();
    m_scrollTimer.start();
}

void BoardCanvas::applyScroll(double canvasPixels, bool fromUser)
{
    const double target = std::clamp(canvasPixels, 0.0, maxScroll());
    if (fromUser) {
        // Quem rolou foi o usuário: o motor não arrasta mais a vista sozinho
        m_userScrolled = true;
        m_scrollTimer.stop();
        // Se o usuário voltou para onde a escrita está, o motor reassume
        if (!m_pending.isNull() && m_pending.top() >= target - 0.5
            && m_pending.bottom() <= target + m_board.screenHeight() + 0.5) {
            m_userScrolled = false;
            m_below->hide();
        }
    }
    if (std::abs(target - m_scroll) < 0.01 && m_viewTop == int(std::round(target))) {
        updateChrome();
        return;
    }
    m_scroll = target;
    rebuildView();
    updateChrome();
    update();
}

// --- Composição da faixa visível -------------------------------------------

void BoardCanvas::rebuildView()
{
    const int screen = m_board.screenHeight();
    if (m_view.width() != m_board.deposit.width() || m_view.height() != screen)
        m_view = QImage(m_board.deposit.width(), screen, QImage::Format_RGB32);
    m_viewTop = std::clamp(int(std::round(m_scroll)), 0, std::max(0, m_board.canvasHeight() - screen));
    composeView(QRect(0, m_viewTop, m_view.width(), screen));
    m_board.deposit.takeDirty(); // acabou de ser composto por inteiro
}

void BoardCanvas::composeView(const QRect &canvasArea)
{
    // Recorta ao que está visível agora
    const QRect band(0, m_viewTop, m_view.width(), m_view.height());
    const QRect area = canvasArea & band & m_board.deposit.bounds();
    if (area.isEmpty())
        return;

    const QRgb chalk = m_params.chalkColor.rgb();
    const int chalkR = qRed(chalk);
    const int chalkG = qGreen(chalk);
    const int chalkB = qBlue(chalk);
    const QColor &color = m_params.boardColor;

    for (int y = area.top(); y <= area.bottom(); ++y) {
        auto *out = reinterpret_cast<QRgb *>(m_view.scanLine(y - m_viewTop));
        const float *deposit = m_board.deposit.data() + m_board.deposit.index(0, y);
        for (int x = area.left(); x <= area.right(); ++x) {
            // Fundo: cor da lousa variada pelo relevo (coordenada absoluta do canvas)
            const float k = 1.0f + m_params.boardVariation * (2.0f * m_board.surface.heightAt(x, y) - 1.0f);
            const int br = scaleChannel(color.red(), k);
            const int bg = scaleChannel(color.green(), k);
            const int bb = scaleChannel(color.blue(), k);
            // cor = mix(corLousa, corGiz, depósito)
            const float t = deposit[x];
            out[x] = qRgb(mixChannel(br, chalkR, t), mixChannel(bg, chalkG, t), mixChannel(bb, chalkB, t));
        }
    }
}

void BoardCanvas::refresh()
{
    const QRect dirty = m_board.deposit.takeDirty();
    if (dirty.isEmpty())
        return;
    composeView(dirty);
    const QRect visible = dirty & QRect(0, m_viewTop, m_view.width(), m_view.height());
    if (!visible.isEmpty())
        update(toWidget(visible));
}

// --- Pintura ----------------------------------------------------------------

void BoardCanvas::paintEvent(QPaintEvent *)
{
    // Só a região pedida pelo update() é redesenhada (o QPainter já vem recortado)
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF board = boardRect();
    painter.drawImage(board, m_view);

    // Giz da mão virtual, por cima da lousa e fora do DepositBuffer
    if (m_chalkVisible && m_giz.visible) {
        const double fade = m_fading
                                ? std::clamp(1.0 - double(m_fadeClock.elapsed()) / std::max(m_giz.fadeMs, 1.0), 0.0, 1.0)
                                : 1.0;
        painter.save();
        painter.translate(board.topLeft());
        const qreal s = board.width() / m_view.width();
        painter.scale(s, s);
        painter.translate(0.0, -double(m_viewTop));
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
    const qreal scale = board.width() / m_view.width();
    const QPointF origin = board.topLeft() - QPointF(0.0, m_viewTop * scale);
    for (const OverlayBox &box : m_overlay) {
        const QRectF r(origin + box.rect.topLeft() * scale, box.rect.size() * scale);
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
        painter.drawLine(QLineF(origin + line.line.p1() * scale, origin + line.line.p2() * scale));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_params.debugVanishingColor);
    for (const QPointF &point : m_overlayPoints)
        painter.drawEllipse(origin + point * scale, m_params.debugPointRadius, m_params.debugPointRadius);
}

void BoardCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateChrome();
    updateCaptionGeometry();
}

// --- Entrada ----------------------------------------------------------------

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

void BoardCanvas::wheelEvent(QWheelEvent *event)
{
    const int steps = event->angleDelta().y();
    if (steps == 0)
        return;
    scrollBy(-steps / 120.0 * m_params.wheelStep);
    event->accept();
}

void BoardCanvas::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_PageDown:
        scrollBy(m_board.screenHeight());
        break;
    case Qt::Key_PageUp:
        scrollBy(-m_board.screenHeight());
        break;
    case Qt::Key_Home:
        setScroll(0.0);
        break;
    case Qt::Key_End:
        setScroll(maxScroll());
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void BoardCanvas::tabletEvent(QTabletEvent *event)
{
    // Mesa digitalizadora: pressão e inclinação reais
    const ChalkSample sample{toCanvas(event->position()), static_cast<float>(event->pressure()),
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
    // A tela visível (uma "página" do canvas) em 16:9, centrada no widget
    const qreal screenWidth = m_view.width();
    const qreal screenHeight = m_view.height();
    const qreal scale = std::min(width() / screenWidth, height() / screenHeight);
    const QSizeF size(screenWidth * scale, screenHeight * scale);
    return QRectF(QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0), size);
}

QPointF BoardCanvas::toCanvas(const QPointF &widgetPos) const
{
    const QRectF board = boardRect();
    const qreal scale = m_view.width() / board.width();
    return (widgetPos - board.topLeft()) * scale + QPointF(0.0, m_viewTop);
}

QRect BoardCanvas::toWidget(const QRect &canvasArea) const
{
    const QRectF board = boardRect();
    const qreal scale = board.width() / m_view.width();
    const QRectF area(board.x() + canvasArea.x() * scale, board.y() + (canvasArea.y() - m_viewTop) * scale,
                      canvasArea.width() * scale, canvasArea.height() * scale);
    // Margem de 1px para a suavização da escala
    return area.toAlignedRect().adjusted(-1, -1, 1, 1);
}

ChalkSample BoardCanvas::mouseSample(const QMouseEvent *event) const
{
    // Mouse: pressão fixa e giz de ponta
    return {toCanvas(event->position()), m_params.mousePressure, m_params.mouseTilt,
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

// --- Enfeites da tela -------------------------------------------------------

void BoardCanvas::updateChrome()
{
    const QRect board = boardRect().toRect();
    const int screens = screenCount();

    m_scrollBar->setRange(0, int(maxScroll()));
    m_scrollBar->setPageStep(m_board.screenHeight());
    {
        QSignalBlocker blocker(m_scrollBar);
        m_scrollBar->setValue(int(std::round(m_scroll)));
    }
    m_scrollBar->setVisible(screens > 1);
    m_scrollBar->setGeometry(board.right() - m_params.scrollBarWidth, board.top(),
                             m_params.scrollBarWidth, board.height());

    m_position->setVisible(screens > 1);
    if (screens > 1) {
        m_position->setText(QString("%1/%2").arg(currentScreen() + 1).arg(screens));
        m_position->adjustSize();
        m_position->move(board.right() - m_params.scrollBarWidth - m_position->width() - 10,
                         board.top() + 10);
    }
    if (m_recording->isVisible()) {
        m_recording->adjustSize();
        m_recording->move(board.left() + 12, board.top() + 12);
    }
    if (m_below->isVisible()) {
        m_below->adjustSize();
        m_below->move(board.center().x() - m_below->width() / 2,
                      board.bottom() - m_below->height() - int(m_captionBandPx * board.height()
                                                               / std::max(1, m_view.height())) - 10);
    }
}

void BoardCanvas::updateCaptionGeometry()
{
    if (!m_caption || !m_caption->isVisible())
        return;
    // Faixa na largura da tela, encostada na borda inferior dela. É overlay da
    // TELA: não faz parte do canvas e não ocupa área útil.
    const QRectF boardF = boardRect();
    const QRect board = boardF.toRect();
    const qreal scale = boardF.width() / m_view.width();
    const int band = static_cast<int>(std::ceil(m_captionBandPx * scale));
    const int h = std::max(band, m_caption->heightForWidth(board.width()));
    m_caption->setGeometry(board.left(), board.bottom() + 1 - h, board.width(), h);
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
