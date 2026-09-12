#include "MainWindow.h"
#include "AskBar.h"
#include "LessonEditor.h"
#include "PlayerBar.h"
#include "TitleBar.h"
#include "TuningPanel.h"
#include "render/BoardCanvas.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHoverEvent>
#include <QMenu>
#include <QMessageBox>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QShortcut>
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
    m_record = m_titleBar->fileMenu()->addAction("Gravar traços", this, &MainWindow::toggleRecording);
    m_record->setCheckable(true);
    m_record->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_titleBar->fileMenu()->addAction("Salvar aula (.jsonl)...", this, &MainWindow::saveLesson)
        ->setShortcut(QKeySequence::Save);

    // Body: lousa com o painel de ajuste (oculto) à direita e a barra do player embaixo
    m_body = new QWidget(this);
    m_body->setObjectName("Body");
    m_canvas = new BoardCanvas(m_board, m_body);
    m_tuningPanel = new TuningPanel(m_body);
    m_tuningPanel->hide();
    auto *playerBar = new PlayerBar(m_body);
    m_askBar = new AskBar(m_body);

    // Log dos comandos recebidos (recolhível, útil para depurar a IA)
    m_log = new QPlainTextEdit(m_body);
    m_log->setObjectName("LessonLog");
    m_log->setReadOnly(true);
    m_log->setFocusPolicy(Qt::NoFocus);
    m_log->setMaximumBlockCount(m_params.logMaxLines);
    m_log->setFixedHeight(m_params.logHeight);
    m_log->hide();

    // Editor da aula (F9)
    m_editor = new LessonEditor(m_body);
    m_editor->hide();

    auto *boardRow = new QHBoxLayout;
    boardRow->setContentsMargins(0, 0, 0, 0);
    boardRow->setSpacing(0);
    boardRow->addWidget(m_canvas, 1);
    boardRow->addWidget(m_tuningPanel);

    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addLayout(boardRow, 1);
    bodyLayout->addWidget(m_log);
    bodyLayout->addWidget(m_editor);
    bodyLayout->addWidget(m_askBar);
    bodyLayout->addWidget(playerBar);
    setCentralWidget(m_body);

    // Aula: a mão desenha no mesmo Board; a lousa só recompõe a região alterada
    connect(&m_player, &LessonPlayer::boardChanged, m_canvas, &BoardCanvas::refresh);
    connect(&m_player, &LessonPlayer::chalkMoved, m_canvas, &BoardCanvas::setChalkPose);
    connect(&m_player, &LessonPlayer::chalkHidden, m_canvas, &BoardCanvas::hideChalk);
    // A lousa rola: o canvas cresce e a vista acompanha a escrita
    connect(&m_player, &LessonPlayer::canvasResized, m_canvas, &BoardCanvas::canvasChanged);
    connect(&m_player, &LessonPlayer::lessonRestarted, m_canvas, &BoardCanvas::resumeFollowing);
    connect(&m_player, &LessonPlayer::viewportRequested, m_canvas, &BoardCanvas::followTo);
    connect(&m_player, &LessonPlayer::speech, m_canvas, &BoardCanvas::setCaption);
    connect(&m_player, &LessonPlayer::stateChanged, playerBar, [this, playerBar] {
        playerBar->setState(m_player.isLoaded(), m_player.isPlaying());
    });
    connect(playerBar, &PlayerBar::playClicked, &m_player, &LessonPlayer::play);
    connect(playerBar, &PlayerBar::pauseClicked, &m_player, &LessonPlayer::pause);
    connect(playerBar, &PlayerBar::restartClicked, &m_player, &LessonPlayer::restart);
    connect(playerBar, &PlayerBar::speedChanged, &m_player, &LessonPlayer::setSpeed);

    // Aula pela IA: o proxy guarda a chave; aqui só chega texto, em pedaços
    connect(m_askBar, &AskBar::asked, this, &MainWindow::askAi);
    connect(m_askBar, &AskBar::stopRequested, this, [this] {
        m_ai.stop();
        m_askBar->setStatus("Resposta interrompida.");
    });
    connect(m_askBar, &AskBar::continueRequested, this, [this] {
        logLine("> continue");
        m_askBar->setStatus("Perguntando...");
        m_streamCommands = 0;
        m_player.startStream(false); // continua a mesma aula, sem limpar a lousa
        m_ai.continueLesson();
    });
    connect(m_askBar, &AskBar::logToggled, m_log, &QWidget::setVisible);
    connect(&m_ai, &AiClient::started, this, [this] { m_askBar->setBusy(true); });
    connect(&m_ai, &AiClient::chunk, &m_player, &LessonPlayer::appendStreamData);
    connect(&m_ai, &AiClient::finished, this, [this] {
        m_askBar->setBusy(false);
        m_askBar->setStatus(m_streamCommands > 0 ? QString() : "A IA não enviou nenhum comando.");
        m_player.finishStream();
    });
    connect(&m_ai, &AiClient::failed, this, [this](const QString &message) {
        m_askBar->setBusy(false);
        m_askBar->setStatus(message);
        logLine("erro: " + message);
        m_player.finishStream();
    });
    connect(&m_player, &LessonPlayer::commandReceived, this, [this](const QJsonObject &command) {
        ++m_streamCommands;
        logLine(QString::fromUtf8(QJsonDocument(command).toJson(QJsonDocument::Compact)));
    });
    connect(&m_player, &LessonPlayer::stepFinished, this, [this] {
        m_askBar->setStepPending(true);
        m_askBar->setStatus("Fim do passo: clique em Continuar.");
    });

    // Painel de ajuste: F10 abre e fecha
    auto *toggleTuning = new QShortcut(QKeySequence(Qt::Key_F10), this);
    connect(toggleTuning, &QShortcut::activated, this, [this] {
        m_tuningPanel->setVisible(!m_tuningPanel->isVisible());
    });
    connect(m_tuningPanel, &TuningPanel::valuesChanged, this, &MainWindow::applyParams);
    connect(m_tuningPanel, &TuningPanel::clearRequested, m_canvas, &BoardCanvas::clear);
    connect(m_tuningPanel, &TuningPanel::saveRequested, this, &MainWindow::saveParams);
    loadParams();

    // Gravação de traços livres: a lousa avisa cada amostra do giz do usuário
    connect(m_canvas, &BoardCanvas::freeStrokeStarted, &m_recorder, &LessonRecorder::beginStroke);
    connect(m_canvas, &BoardCanvas::freeSample, &m_recorder, &LessonRecorder::addSample);
    connect(m_canvas, &BoardCanvas::freeStrokeFinished, &m_recorder, &LessonRecorder::endStroke);
    connect(&m_recorder, &LessonRecorder::commandRecorded, &m_player, &LessonPlayer::appendCommand);
    connect(&m_recorder, &LessonRecorder::recordingChanged, this, [this](bool on) {
        m_canvas->setRecording(on);
        m_record->setChecked(on);
        if (!on && m_editor->isVisible())
            m_editor->setText(m_player.lessonText());
    });

    // Editor da aula: F9 abre e fecha
    auto *toggleEditor = new QShortcut(QKeySequence(Qt::Key_F9), this);
    connect(toggleEditor, &QShortcut::activated, this, [this] {
        const bool show = !m_editor->isVisible();
        if (show)
            m_editor->setText(m_player.lessonText());
        m_editor->setVisible(show);
    });
    connect(m_editor, &LessonEditor::applyRequested, this, &MainWindow::applyEditor);
    connect(m_editor, &LessonEditor::saveRequested, this, &MainWindow::saveLesson);
    connect(m_editor, &LessonEditor::openRequested, this, &MainWindow::openLesson);

    // View: mostrar ou não o giz da mão virtual (o mesmo valor está no F10)
    m_showChalk = m_titleBar->viewMenu()->addAction("Mostrar giz");
    m_showChalk->setCheckable(true);
    m_showChalk->setChecked(m_canvas->gizParams().visible);
    connect(m_showChalk, &QAction::toggled, this, [this](bool on) {
        TunableParams values = m_tuningPanel->values();
        if (bool(values.giz.visible) == on)
            return;
        values.giz.visible = on;
        m_tuningPanel->setValues(values);
        applyParams(values);
    });

    // Modo de depuração: F12 mostra/esconde as bounding boxes e ids
    auto *toggleDebug = new QShortcut(QKeySequence(Qt::Key_F12), this);
    connect(toggleDebug, &QShortcut::activated, this, [this] {
        m_canvas->setOverlayVisible(!m_canvas->isOverlayVisible());
    });
    connect(&m_player.scene(), &Scene::elementsChanged, this, &MainWindow::updateOverlay);
    updateOverlay();

    // A legenda ocupa a faixa que a cena reserva para ela na base da lousa
    const SceneParams &scene = m_player.scene().params();
    m_canvas->setCaptionBand(scene.captionBandHeight * m_board.params().boardWidth / scene.boardWidth);

    setWindowTitle("Lousa Inteligente");

    // Intercepta cliques sobre as bordas em qualquer widget desta janela
    // (ex.: o botão fechar no canto superior direito)
    qApp->installEventFilter(this);
}

void MainWindow::updateOverlay()
{
    // Unidades da lousa → pixels da lousa
    const Scene &scene = m_player.scene();
    const double pixelsPerUnit = m_board.params().boardWidth / scene.params().boardWidth;
    const auto toPixels = [pixelsPerUnit](const QRectF &r) {
        return QRectF(r.topLeft() * pixelsPerUnit, r.size() * pixelsPerUnit);
    };
    std::vector<OverlayBox> boxes;
    boxes.push_back({toPixels(scene.usableArea()), "área útil", true});
    for (const SceneElement &element : scene.elements())
        boxes.push_back({toPixels(element.bounds), element.label, false});
    m_canvas->setOverlay(boxes);

    // Objetos 3D: arestas ocultas e linhas de fuga (mesmo com "omitir")
    const SceneDebugGeometry geometry = scene.debugGeometry();
    std::vector<OverlayLine> lines;
    const auto addPolylines = [&lines, pixelsPerUnit](const std::vector<Polyline> &polylines, bool vanishing) {
        for (const Polyline &polyline : polylines)
            for (std::size_t i = 1; i < polyline.size(); ++i)
                lines.push_back({QLineF(polyline[i - 1] * pixelsPerUnit, polyline[i] * pixelsPerUnit), vanishing});
    };
    addPolylines(geometry.hiddenLines, false);
    addPolylines(geometry.vanishingLines, true);
    std::vector<QPointF> points;
    for (const QPointF &point : geometry.vanishingPoints)
        points.push_back(point * pixelsPerUnit);
    m_canvas->setOverlayGeometry(lines, points);
}

void MainWindow::askAi(const QString &question)
{
    logLine("> " + question);
    m_askBar->setStatus("Perguntando...");
    m_streamCommands = 0;
    m_player.startStream(); // lousa limpa: começa uma aula nova
    m_ai.ask(question);
}

void MainWindow::logLine(const QString &text)
{
    m_log->appendPlainText(text);
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
    else if (m_editor->isVisible())
        m_editor->setText(m_player.lessonText());
}

void MainWindow::saveLesson()
{
    QString path = QFileDialog::getSaveFileName(this, "Salvar aula", QString(),
                                                "Aulas (*.jsonl);;Todos os arquivos (*)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".jsonl", Qt::CaseInsensitive))
        path += ".jsonl";
    // O que estiver no editor é o que vale, se ele estiver aberto
    if (m_editor->isVisible()) {
        QStringList errors;
        if (!m_player.applyText(m_editor->text(), &errors)) {
            m_editor->setErrors(errors);
            return;
        }
    }
    QString error;
    if (!m_player.save(path, &error))
        QMessageBox::warning(this, "Salvar aula", error);
}

void MainWindow::toggleRecording()
{
    m_recorder.setPixelsPerUnit(m_player.handParams().pixelsPerUnit);
    m_recorder.setRecording(!m_recorder.isRecording());
}

void MainWindow::applyEditor()
{
    QStringList errors;
    if (m_player.applyText(m_editor->text(), &errors))
        m_editor->setErrors({});
    else
        m_editor->setErrors(errors);
}

void MainWindow::applyParams(const TunableParams &values)
{
    if (m_board.setParams(values.physics))
        m_canvas->rebuildSurface();
    m_player.setHandParams(values.hand);
    m_player.setHumanizerParams(values.humanizer);

    GizParams giz = values.giz;
    giz.pixelsPerUnit = values.hand.pixelsPerUnit;
    m_canvas->setGizParams(giz);
    if (m_showChalk && m_showChalk->isChecked() != giz.visible) {
        QSignalBlocker blocker(m_showChalk);
        m_showChalk->setChecked(giz.visible);
    }
}

void MainWindow::loadParams()
{
    TunableParams values{m_board.params(), m_player.handParams(), m_canvas->gizParams(),
                         m_player.humanizerParams()};
    const QString path = paramsPath();
    if (QFile::exists(path)) {
        QString error;
        if (TuningPanel::load(path, &values, &error)) {
            applyParams(values);
            m_tuningPanel->setStatus(QString("Carregado de %1").arg(QDir::toNativeSeparators(path)));
        } else {
            qWarning().noquote() << "params.json ignorado:" << error;
            m_tuningPanel->setStatus(QString("Erro ao ler %1: %2").arg(QDir::toNativeSeparators(path), error));
        }
    }
    m_tuningPanel->setValues(values);
}

void MainWindow::saveParams()
{
    const QString path = paramsPath();
    QString error;
    if (TuningPanel::save(path, m_tuningPanel->values(), &error))
        m_tuningPanel->setStatus(QString("Salvo em %1").arg(QDir::toNativeSeparators(path)));
    else
        m_tuningPanel->setStatus(QString("Erro ao salvar %1: %2").arg(QDir::toNativeSeparators(path), error));
}

QString MainWindow::paramsPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("params.json");
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
