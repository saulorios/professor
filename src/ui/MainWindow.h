#pragma once

#include "LessonPlayer.h"
#include "physics/Board.h"

#include <QMainWindow>
#include <QSize>

class BoardCanvas;
class TitleBar;
class TuningPanel;
struct TunableParams;

// Parâmetros ajustáveis da janela principal
struct MainWindowParams {
    QSize initialSize{1280, 800};
    QSize minimumSize{800, 500};
    int resizeMargin = 5; // largura (px) da faixa junto às bordas que permite redimensionar
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
    TitleBar *m_titleBar = nullptr;
    QWidget *m_body = nullptr;
    BoardCanvas *m_canvas = nullptr;
    TuningPanel *m_tuningPanel = nullptr;
    Qt::Edges m_cursorEdges; // bordas refletidas no cursor atual
};
