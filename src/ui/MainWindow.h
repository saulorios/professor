#pragma once

#include "LessonPlayer.h"
#include "physics/Board.h"

#include <QMainWindow>
#include <QSize>

class TitleBar;

// Parâmetros ajustáveis da janela principal
struct MainWindowParams {
    QSize initialSize{1280, 800};
    QSize minimumSize{800, 500};
    int resizeMargin = 5; // largura (px) da faixa junto às bordas que permite redimensionar
};

// Janela principal sem moldura nativa: TitleBar customizada + body com a lousa
// e a barra do player. O redimensionamento pelas bordas usa QWindow::startSystemResize.
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

    // Bordas da janela sob a posição informada (em coordenadas locais)
    Qt::Edges edgesAt(const QPoint &pos) const;
    void updateCursorShape(Qt::Edges edges);

    MainWindowParams m_params;
    Board m_board;          // estado físico compartilhado por mouse e mão virtual
    LessonPlayer m_player;
    TitleBar *m_titleBar = nullptr;
    QWidget *m_body = nullptr;
    Qt::Edges m_cursorEdges; // bordas refletidas no cursor atual
};
