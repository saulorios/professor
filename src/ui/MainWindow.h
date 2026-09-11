#pragma once

#include <QMainWindow>
#include <QSize>

class TitleBar;

// Parâmetros ajustáveis da janela principal
struct MainWindowParams {
    QSize initialSize{1280, 800};
    QSize minimumSize{800, 500};
    int resizeMargin = 5; // largura (px) da faixa junto às bordas que permite redimensionar
};

// Janela principal sem moldura nativa: TitleBar customizada + body.
// O redimensionamento pelas bordas é feito com QWindow::startSystemResize.
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
    // Bordas da janela sob a posição informada (em coordenadas locais)
    Qt::Edges edgesAt(const QPoint &pos) const;
    void updateCursorShape(Qt::Edges edges);

    MainWindowParams m_params;
    TitleBar *m_titleBar = nullptr;
    QWidget *m_body = nullptr;
};
