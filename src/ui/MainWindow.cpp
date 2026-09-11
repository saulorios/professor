#include "MainWindow.h"
#include "PlayerBar.h"
#include "TitleBar.h"
#include "render/BoardCanvas.h"

#include <QApplication>
#include <QFileDialog>
#include <QHoverEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWindow>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_player(m_board)
{
    // Remove a moldura nativa do sistema
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    // Recebe eventos HoverMove mesmo com o mouse sobre os widgets filhos,
    // usados para trocar o cursor perto das bordas
    setAttribute(Qt::WA_Hover);

    resize(m_params.initialSize);
    setMinimumSize(m_params.minimumSize);

    // TitleBar customizada, ocupando o lugar do menu da QMainWindow
    m_titleBar = new TitleBar(this);
    setMenuWidget(m_titleBar);
    connect(this, &QWidget::windowTitleChanged, m_titleBar, &TitleBar::setTitle);
    m_titleBar->fileMenu()->addAction("Abrir aula (.jsonl)...", this, &MainWindow::openLesson);

    // Body: lousa ocupando o espaço e a barra do player embaixo
    m_body = new QWidget(this);
    m_body->setObjectName("Body");
    auto *canvas = new BoardCanvas(m_board, m_body);
    auto *playerBar = new PlayerBar(m_body);
    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(canvas, 1);
    bodyLayout->addWidget(playerBar);
    setCentralWidget(m_body);

    // Aula: a mão desenha no mesmo Board; a lousa só recompõe a região alterada
    connect(&m_player, &LessonPlayer::boardChanged, canvas, &BoardCanvas::refresh);
    connect(&m_player, &LessonPlayer::speech, canvas, &BoardCanvas::setCaption);
    connect(&m_player, &LessonPlayer::stateChanged, playerBar, [this, playerBar] {
        playerBar->setState(m_player.isLoaded(), m_player.isPlaying());
    });
    connect(playerBar, &PlayerBar::playClicked, &m_player, &LessonPlayer::play);
    connect(playerBar, &PlayerBar::pauseClicked, &m_player, &LessonPlayer::pause);
    connect(playerBar, &PlayerBar::restartClicked, &m_player, &LessonPlayer::restart);
    connect(playerBar, &PlayerBar::speedChanged, &m_player, &LessonPlayer::setSpeed);

    setWindowTitle("Professor - Meu App");

    // Intercepta cliques sobre as bordas em qualquer widget desta janela
    // (ex.: o botão fechar no canto superior direito)
    qApp->installEventFilter(this);
}

void MainWindow::openLesson()
{
    const QString path = QFileDialog::getOpenFileName(this, "Abrir aula", QString(),
                                                      "Aulas (*.jsonl);;Todos os arquivos (*)");
    if (path.isEmpty())
        return;
    QString error;
    if (!m_player.open(path, &error))
        QMessageBox::warning(this, "Abrir aula", error);
}

bool MainWindow::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::HoverMove:
        updateCursorShape(edgesAt(static_cast<QHoverEvent *>(event)->position().toPoint()));
        break;
    case QEvent::HoverLeave:
        updateCursorShape({});
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
            // Só consome o clique se o sistema realmente iniciou o redimensionamento
            if (edges && windowHandle() && windowHandle()->startSystemResize(edges))
                return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
    // Mantém o ícone maximizar/restaurar sincronizado com o estado da janela
    if (event->type() == QEvent::WindowStateChange && m_titleBar) {
        m_titleBar->setMaximized(isMaximized());
        updateCursorShape({});
    }
    QMainWindow::changeEvent(event);
}

Qt::Edges MainWindow::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;

    // Janela maximizada ou em tela cheia não é redimensionável pelas bordas
    if (isMaximized() || isFullScreen())
        return edges;

    const int margin = m_params.resizeMargin;
    if (pos.x() < margin)
        edges |= Qt::LeftEdge;
    if (pos.x() >= width() - margin)
        edges |= Qt::RightEdge;
    if (pos.y() < margin)
        edges |= Qt::TopEdge;
    if (pos.y() >= height() - margin)
        edges |= Qt::BottomEdge;

    return edges;
}

void MainWindow::updateCursorShape(Qt::Edges edges)
{
    // Evita trocar o cursor do sistema a cada movimento do mouse
    if (edges == m_cursorEdges)
        return;
    m_cursorEdges = edges;

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
