#pragma once

#include <QMainWindow>

class TitleBar;

// Janela principal sem moldura nativa: topbar customizada + body.
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

    TitleBar *m_titleBar = nullptr;
    QWidget *m_body = nullptr;
};
