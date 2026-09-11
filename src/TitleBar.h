#pragma once

#include <QColor>
#include <QPoint>
#include <QPushButton>
#include <QWidget>

class QLabel;
class QMenuBar;

// Botão de controle da janela (minimizar, maximizar/restaurar, fechar).
// O fundo vem do QSS; o ícone é desenhado com QPainter e suas cores também
// são definidas no QSS (qproperty-iconColor / qproperty-iconHoverColor).
class TitleBarButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(QColor iconColor READ iconColor WRITE setIconColor)
    Q_PROPERTY(QColor iconHoverColor READ iconHoverColor WRITE setIconHoverColor)

public:
    enum class Kind { Minimize, Maximize, Restore, Close };

    explicit TitleBarButton(Kind kind, QWidget *parent = nullptr);

    void setKind(Kind kind);

    QColor iconColor() const { return m_iconColor; }
    void setIconColor(const QColor &color);

    QColor iconHoverColor() const { return m_iconHoverColor; }
    void setIconHoverColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Kind m_kind;
    QColor m_iconColor = QColor(0xCC, 0xCC, 0xCC);
    QColor m_iconHoverColor = QColor(0xCC, 0xCC, 0xCC);
};

// Barra de título customizada: logo + menu à esquerda, título ao centro
// e botões de controle à direita. Arrastar move a janela; duplo clique
// alterna entre maximizar e restaurar.
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

public slots:
    void setTitle(const QString &title);
    void setMaximized(bool maximized);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void toggleMaximize();
    void updateTitleGeometry();

    QLabel *m_logo = nullptr;
    QMenuBar *m_menuBar = nullptr;
    QLabel *m_titleLabel = nullptr;
    TitleBarButton *m_minimizeButton = nullptr;
    TitleBarButton *m_maximizeButton = nullptr;
    TitleBarButton *m_closeButton = nullptr;

    // Estado do arraste (o movimento nativo só começa após um pequeno deslocamento)
    QPoint m_pressPos;
    bool m_dragPending = false;
};
