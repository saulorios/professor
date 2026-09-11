#include "TitleBar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

namespace {
constexpr int kTitleBarHeight = 35;   // altura útil da topbar
constexpr int kSeparatorHeight = 1;   // linha separadora (border-bottom no QSS)
constexpr int kButtonWidth = 46;
constexpr int kIconSize = 10;         // lado dos ícones dos botões
}

// ---------------------------------------------------------------------------
// TitleBarButton
// ---------------------------------------------------------------------------

TitleBarButton::TitleBarButton(Kind kind, QWidget *parent)
    : QPushButton(parent)
    , m_kind(kind)
{
    setFixedSize(kButtonWidth, kTitleBarHeight);
    setFocusPolicy(Qt::NoFocus);
}

void TitleBarButton::setKind(Kind kind)
{
    if (m_kind == kind)
        return;
    m_kind = kind;
    update();
}

void TitleBarButton::setIconColor(const QColor &color)
{
    m_iconColor = color;
    update();
}

void TitleBarButton::setIconHoverColor(const QColor &color)
{
    m_iconHoverColor = color;
    update();
}

void TitleBarButton::paintEvent(QPaintEvent *event)
{
    // Fundo (normal e :hover) desenhado a partir do QSS
    QPushButton::paintEvent(event);

    QPainter painter(this);
    QPen pen(underMouse() ? m_iconHoverColor : m_iconColor);
    pen.setWidthF(1.0);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Área do ícone, centralizada no botão
    const int s = kIconSize;
    const int x = (width() - s) / 2;
    const int y = (height() - s) / 2;

    switch (m_kind) {
    case Kind::Minimize:
        painter.drawLine(x, y + s / 2, x + s - 1, y + s / 2);
        break;

    case Kind::Maximize:
        painter.drawRect(x, y, s - 1, s - 1);
        break;

    case Kind::Restore:
        // Janela da frente
        painter.drawRect(x, y + 2, s - 3, s - 3);
        // Janela de trás (apenas as partes não cobertas pela da frente)
        painter.drawLine(x + 2, y, x + s - 1, y);
        painter.drawLine(x + s - 1, y, x + s - 1, y + s - 3);
        painter.drawLine(x + 2, y, x + 2, y + 1);
        painter.drawLine(x + s - 2, y + s - 3, x + s - 1, y + s - 3);
        break;

    case Kind::Close:
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawLine(QPointF(x, y), QPointF(x + s, y + s));
        painter.drawLine(QPointF(x + s, y), QPointF(x, y + s));
        break;
    }
}

// ---------------------------------------------------------------------------
// TitleBar
// ---------------------------------------------------------------------------

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    // Necessário para que o QSS (fundo e borda) seja aplicado a uma subclasse de QWidget
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(kTitleBarHeight + kSeparatorHeight);

    // Logo: placeholder 16x16 estilizado no QSS
    m_logo = new QLabel(this);
    m_logo->setObjectName("Logo");
    m_logo->setFixedSize(16, 16);
    m_logo->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Menu embutido na topbar
    m_menuBar = new QMenuBar(this);
    m_menuBar->setNativeMenuBar(false); // mantém o menu dentro da janela (sem menu global do sistema)
    m_menuBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    const QStringList menus = {"File", "Edit", "Selection", "View", "Go", "Run", "Terminal", "Help"};
    for (const QString &menu : menus)
        m_menuBar->addMenu(menu);

    // Título: fica fora do layout e é posicionado em updateTitleGeometry(),
    // para ficar centralizado em relação à janela inteira
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName("TitleLabel");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Botões de controle da janela
    m_minimizeButton = new TitleBarButton(TitleBarButton::Kind::Minimize, this);
    m_maximizeButton = new TitleBarButton(TitleBarButton::Kind::Maximize, this);
    m_closeButton = new TitleBarButton(TitleBarButton::Kind::Close, this);
    m_closeButton->setObjectName("CloseButton");

    connect(m_minimizeButton, &QPushButton::clicked, this, [this] { window()->showMinimized(); });
    connect(m_maximizeButton, &QPushButton::clicked, this, &TitleBar::toggleMaximize);
    connect(m_closeButton, &QPushButton::clicked, this, [this] { window()->close(); });

    auto *layout = new QHBoxLayout(this);
    // Margem inferior reservada para a linha separadora desenhada pelo QSS
    layout->setContentsMargins(10, 0, 0, kSeparatorHeight);
    layout->setSpacing(0);
    layout->addWidget(m_logo, 0, Qt::AlignVCenter);
    layout->addSpacing(6);
    layout->addWidget(m_menuBar, 0, Qt::AlignVCenter);
    layout->addStretch(1);
    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);
}

void TitleBar::setTitle(const QString &title)
{
    m_titleLabel->setText(title);
    updateTitleGeometry();
}

void TitleBar::setMaximized(bool maximized)
{
    m_maximizeButton->setKind(maximized ? TitleBarButton::Kind::Restore
                                        : TitleBarButton::Kind::Maximize);
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->position().toPoint();
        m_dragPending = true;
    }
    event->accept();
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    // O movimento nativo só começa depois de um pequeno arraste,
    // para não interferir no duplo clique
    if (m_dragPending && (event->buttons() & Qt::LeftButton)
        && (event->position().toPoint() - m_pressPos).manhattanLength()
               >= QApplication::startDragDistance()) {
        m_dragPending = false;
        if (QWindow *handle = window()->windowHandle())
            handle->startSystemMove();
    }
    event->accept();
}

void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragPending = false;
    event->accept();
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPending = false;
        toggleMaximize();
    }
    event->accept();
}

void TitleBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTitleGeometry();
}

void TitleBar::toggleMaximize()
{
    QWidget *win = window();
    if (win->isMaximized())
        win->showNormal();
    else
        win->showMaximized();
}

void TitleBar::updateTitleGeometry()
{
    // Espaço livre entre o fim do menu e o início dos botões
    const int minX = m_menuBar->geometry().right() + 1;
    const int maxX = m_minimizeButton->geometry().left();
    const int textWidth = qMin(m_titleLabel->sizeHint().width(), qMax(0, maxX - minX));

    // Centraliza em relação à janela, mas sem invadir o menu nem os botões
    const int x = qBound(minX, (width() - textWidth) / 2, maxX - textWidth);
    m_titleLabel->setGeometry(x, 0, textWidth, kTitleBarHeight);
}
