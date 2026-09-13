#pragma once

#include "LessonPlayer.h"
#include "LessonRecorder.h"
#include "handwriting/data/GlyphDatabase.h"
#include "physics/Board.h"
#include "protocol/AiClient.h"
#include "protocol/ProxyLauncher.h"

#include <QMainWindow>
#include <QSize>

class AgentPanel;
class HandwritingRecorder;
class BoardCanvas;
class LessonEditor;
class QAction;
class QSplitter;
class QPlainTextEdit;
class TitleBar;
class TuningPanel;
struct TunableParams;

// Parâmetros ajustáveis da janela principal
struct MainWindowParams {
    QSize initialSize{1280, 800};
    QSize minimumSize{800, 500};
    int resizeMargin = 5; // largura (px) da faixa junto às bordas que permite redimensionar
    int minimumBoardWidth = 520; // o splitter não deixa a lousa menor que isto
    int logHeight = 150;  // altura do painel de log (recolhível)
    int logMaxLines = 500;
};

// Janela principal sem moldura nativa: TitleBar customizada + body com a lousa,
// o painel de ajuste (F10) e a barra do player. O redimensionamento pelas
// bordas usa QWindow::startSystemResize.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void openLesson();
    void saveLesson();
    void toggleRecording();
    void applyEditor();
    void newLesson();

    // Aula vinda da IA (pelo proxy) e log dos comandos recebidos
    void askAi(const QString &question);
    // Letras gravadas no banco de escrita manual: recarrega e aplica à escrita
    void reloadHandwriting();
    void logLine(const QString &text);

    // Modo de depuração (F12): bounding boxes e ids dos elementos da cena
    void updateOverlay();

    // Painel de ajuste: aplica os valores e os carrega/grava em params.json,
    // na pasta do executável
    void applyParams(const TunableParams &values);
    void loadParams();
    void saveParams();
    QString paramsPath() const;

    // Bordas da janela sob a posição informada (em coordenadas locais)
    Qt::Edges edgesAt(const QPoint &pos) const;
    void updateCursorShape(Qt::Edges edges);

    MainWindowParams m_params;
    Board m_board;          // estado físico compartilhado por mouse e mão virtual
    LessonPlayer m_player;
    LessonRecorder m_recorder;
    AiClient m_ai;
    ProxyLauncher m_proxy;   // sobe o proxy Python junto com o app
    TitleBar *m_titleBar = nullptr;
    AgentPanel *m_agent = nullptr;
    QSplitter *m_splitter = nullptr;
    QPlainTextEdit *m_log = nullptr;
    LessonEditor *m_editor = nullptr;
    HandwritingRecorder *m_handwriting = nullptr;
    handwriting::GlyphDatabase m_glyphs;     // banco usado pela escrita da lousa
    QAction *m_useHandwriting = nullptr;
    QAction *m_showChalk = nullptr;
    QAction *m_record = nullptr;
    QAction *m_showPanel = nullptr;
    double m_answerTop = 0.0;   // topo, em px do canvas, da resposta em andamento
    int m_streamCommands = 0; // comandos recebidos na resposta atual
    QString m_streamError;    // erro relatado pelo proxy no meio da resposta atual
    QWidget *m_body = nullptr;
    BoardCanvas *m_canvas = nullptr;
    TuningPanel *m_tuningPanel = nullptr;
    Qt::Edges m_cursorEdges; // bordas refletidas no cursor atual
};
