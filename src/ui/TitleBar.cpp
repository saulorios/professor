#include "TitleBar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

// ---------------------------------------------------------------------------
// TitleBarButton
// ---------------------------------------------------------------------------

TitleBarButton::TitleBarButton(Kind kind, const TitleBarParams &params, QWidget *parent)
    : QPushButton(parent)
    , m_kind(kind)
    , m_iconSize(params.iconSize)
    , m_hoverSize(params.hoverSize)
    , m_iconStroke(params.iconStroke)
    , m_restoreOffset(params.restoreOffset)
    , m_closeStroke(params.closeStroke)
{
    setFixedSize(params.buttonWidth, params.height);
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

void TitleBarButton::setHoverColor(const QColor &color)
{
    m_hoverColor = color;
    update();
}

void TitleBarButton::paintEvent(QPaintEvent *event)
{
    // Fundo normal (transparente) vindo do QSS; o QSS não tem :hover
    QPushButton::paintEvent(event);

    QPainter painter(this);
    const bool hovered = underMouse();

    // Hover: círculo centralizado, com borda nítida (sem antialiasing), igual
    // para os três botões. A área de clique continua sendo o botão inteiro.
    if (hovered) {
        const int d = m_hoverSize;
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_hoverColor);
        painter.drawEllipse(QRect((width() - d) / 2, (height() - d) / 2, d, d));
    }

    // Ícone numa caixa s × s, alinhada ao pixel e centralizada no botão.
    // Retas são retângulos cheios de `t` px (nítidos em qualquer tema).
    const QColor color = hovered ? m_iconHoverColor : m_iconColor;
    const int s = m_iconSize;
    const int t = m_iconStroke;
    const int x = (width() - s) / 2;
    const int y = (height() - s) / 2;

    switch (m_kind) {
    case Kind::Minimize:
        // Barra na base da caixa
        painter.fillRect(QRect(x, y + s - t, s, t), color);
        break;

    case Kind::Maximize:
        painter.fillRect(QRect(x, y, s, t), color);
        painter.fillRect(QRect(x, y + s - t, s, t), color);
        painter.fillRect(QRect(x, y, t, s), color);
        painter.fillRect(QRect(x + s - t, y, t, s), color);
        break;

    case Kind::Restore: {
        // Janela de trás: quadrado completo no canto superior direito
        const int back = s - m_restoreOffset;
        const int bx = x + m_restoreOffset;
        painter.fillRect(QRect(bx, y, back, t), color);
        painter.fillRect(QRect(bx, y + back - t, back, t), color);
        painter.fillRect(QRect(bx, y, t, back), color);
        painter.fillRect(QRect(bx + back - t, y, t, back), color);
        // Janela da frente: só o "L" (lado esquerdo e base) que sobra por fora
        const int front = s - t;
        const int fy = y + t;
        painter.fillRect(QRect(x, fy, t, front), color);
        painter.fillRect(QRect(x, fy + front - t, front, t), color);
        break;
    }

    case Kind::Close: {
        // Diagonais suavizadas, dos centros dos pixels dos cantos
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(color, m_closeStroke, Qt::SolidLine, Qt::FlatCap);
        painter.setPen(pen);
        const double a = 0.5;
        const double b = s - 0.5;
        painter.drawLine(QPointF(x + a, y + a), QPointF(x + b, y + b));
        painter.drawLine(QPointF(x + b, y + a), QPointF(x + a, y + b));
        break;
    }
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
    setFixedHeight(m_params.height + m_params.separatorHeight);

    // Logo: placeholder estilizado no QSS
    m_logo = new QLabel(this);
    m_logo->setObjectName("Logo");
    m_logo->setFixedSize(m_params.logoSize, m_params.logoSize);
    m_logo->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Menu embutido na topbar
    m_menuBar = new QMenuBar(this);
    m_menuBar->setNativeMenuBar(false); // mantém o menu dentro da janela (sem menu global do sistema)
    m_menuBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    const QStringList menus = {"File", "Edit", "Selection", "View", "Go", "Run", "Terminal", "Help"};
    for (const QString &menu : menus) {
        QMenu *added = m_menuBar->addMenu(menu);
        if (menu == "File")
            m_fileMenu = added;
        else if (menu == "View")
            m_viewMenu = added;
    }

    // Título: fica fora do layout e é posicionado em updateTitleGeometry(),
    // para ficar centralizado em relação à janela inteira
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName("TitleLabel");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Botões de controle da janela
    m_minimizeButton = new TitleBarButton(TitleBarButton::Kind::Minimize, m_params, this);
    m_maximizeButton = new TitleBarButton(TitleBarButton::Kind::Maximize, m_params, this);
    m_closeButton = new TitleBarButton(TitleBarButton::Kind::Close, m_params, this);
    m_closeButton->setObjectName("CloseButton");

    connect(m_minimizeButton, &QPushButton::clicked, this, [this] { window()->showMinimized(); });
    connect(m_maximizeButton, &QPushButton::clicked, this, &TitleBar::toggleMaximize);
    connect(m_closeButton, &QPushButton::clicked, this, [this] { window()->close(); });

    auto *layout = new QHBoxLayout(this);
    // Margem inferior reservada para a linha separadora desenhada pelo QSS
    layout->setContentsMargins(m_params.leftMargin, 0, 0, m_params.separatorHeight);
    layout->setSpacing(0);
    layout->addWidget(m_logo, 0, Qt::AlignVCenter);
    layout->addSpacing(m_params.logoSpacing);
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
    m_titleLabel->setGeometry(x, 0, textWidth, m_params.height);
}
