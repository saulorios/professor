#include "MainWindow.h"
#include "TitleBar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QWindow>

namespace {
// Largura (em px) da faixa junto às bordas que permite redimensionar
constexpr int kResizeMargin = 5;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Remove a moldura nativa do sistema
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    // Recebe eventos HoverMove mesmo com o mouse sobre os widgets filhos,
    // usados para trocar o cursor perto das bordas
    setAttribute(Qt::WA_Hover);

    resize(1280, 800);
    setMinimumSize(800, 500);

    // Topbar customizada, ocupando o lugar do menu da QMainWindow
    m_titleBar = new TitleBar(this);
    setMenuWidget(m_titleBar);
    connect(this, &QWidget::windowTitleChanged, m_titleBar, &TitleBar::setTitle);

    // Body: por enquanto vazio. O layout horizontal já está pronto para
    // receber, no futuro, uma sidebar à esquerda e o editor central.
    m_body = new QWidget(this);
    m_body->setObjectName("Body");
    auto *bodyLayout = new QHBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    setCentralWidget(m_body);

    setWindowTitle("Professor - Meu App");

    // Intercepta cliques sobre as bordas em qualquer widget desta janela
    // (ex.: o botão fechar no canto superior direito)
    qApp->installEventFilter(this);
}

bool MainWindow::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::HoverMove:
        updateCursorShape(edgesAt(static_cast<QHoverEvent *>(event)->position().toPoint()));
        break;
    case QEvent::HoverLeave:
        unsetCursor();
        break;
    default:
        break;
    }
    return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Clique com o botão esquerdo sobre uma borda inicia o redimensionamento nativo
    if (event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
        auto *widget = static_cast<QWidget *>(watched);
        auto *mouseEvent = static_cast<QMouseEvent *>(event);

        if (widget->window() == this && mouseEvent->button() == Qt::LeftButton) {
            const Qt::Edges edges = edgesAt(mapFromGlobal(mouseEvent->globalPosition().toPoint()));
            if (edges && windowHandle()) {
                windowHandle()->startSystemResize(edges);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
    // Mantém o ícone maximizar/restaurar sincronizado com o estado da janela
    if (event->type() == QEvent::WindowStateChange && m_titleBar) {
        m_titleBar->setMaximized(isMaximized());
        unsetCursor();
    }
    QMainWindow::changeEvent(event);
}

Qt::Edges MainWindow::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;

    // Janela maximizada ou em tela cheia não é redimensionável pelas bordas
    if (isMaximized() || isFullScreen())
        return edges;

    if (pos.x() < kResizeMargin)
        edges |= Qt::LeftEdge;
    if (pos.x() >= width() - kResizeMargin)
        edges |= Qt::RightEdge;
    if (pos.y() < kResizeMargin)
        edges |= Qt::TopEdge;
    if (pos.y() >= height() - kResizeMargin)
        edges |= Qt::BottomEdge;

    return edges;
}

void MainWindow::updateCursorShape(Qt::Edges edges)
{
    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
        setCursor(Qt::SizeFDiagCursor);
    else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
        setCursor(Qt::SizeBDiagCursor);
    else if (edges & (Qt::LeftEdge | Qt::RightEdge))
        setCursor(Qt::SizeHorCursor);
    else if (edges & (Qt::TopEdge | Qt::BottomEdge))
        setCursor(Qt::SizeVerCursor);
    else
        unsetCursor();
}
