#pragma once

#include <QColor>
#include <QPoint>
#include <QPushButton>
#include <QWidget>

class QLabel;
class QMenu;
class QMenuBar;
class QSvgWidget;

// Parâmetros ajustáveis da TitleBar (em pixels)
struct TitleBarParams {
    int height = 35;          // altura útil da topbar
    int separatorHeight = 1;  // linha separadora (border-bottom no QSS)
    int buttonWidth = 32;     // área de clique dos botões minimizar / maximizar / fechar
    int iconSize = 10;        // lado da caixa dos ícones
    int iconStroke = 2;       // espessura das retas dos ícones
    int restoreOffset = 3;    // deslocamento da janela de trás no ícone restaurar
    double closeStroke = 2.0; // espessura das diagonais do fechar (suavizadas)
    int hoverSize = 28;       // diâmetro do círculo de hover (menor que a área de clique)
    int logoSize = 16;
    int leftMargin = 10;      // espaço antes do logo
    int logoSpacing = 6;      // espaço entre o logo e o menu
};

// Botão de controle da janela (minimizar, maximizar/restaurar, fechar).
// O botão desenha tudo com QPainter: o hover é um círculo centralizado (a área
// de clique continua sendo o botão inteiro) e o ícone vem por cima. As cores
// vêm do QSS (qproperty-hoverColor / qproperty-iconColor / qproperty-iconHoverColor).
class TitleBarButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(QColor iconColor READ iconColor WRITE setIconColor)
    Q_PROPERTY(QColor iconHoverColor READ iconHoverColor WRITE setIconHoverColor)
    Q_PROPERTY(QColor hoverColor READ hoverColor WRITE setHoverColor)

public:
    enum class Kind { Minimize, Maximize, Restore, Close };

    TitleBarButton(Kind kind, const TitleBarParams &params, QWidget *parent = nullptr);

    void setKind(Kind kind);

    QColor iconColor() const { return m_iconColor; }
    void setIconColor(const QColor &color);

    QColor iconHoverColor() const { return m_iconHoverColor; }
    void setIconHoverColor(const QColor &color);

    QColor hoverColor() const { return m_hoverColor; }
    void setHoverColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Kind m_kind;
    int m_iconSize;
    int m_hoverSize;
    int m_iconStroke;
    int m_restoreOffset;
    double m_closeStroke;
    QColor m_iconColor = QColor(0xCC, 0xCC, 0xCC);
    QColor m_iconHoverColor = QColor(0xFF, 0xFF, 0xFF);
    QColor m_hoverColor = QColor(0x2A, 0x2A, 0x2A);
};

// Barra de título customizada: logo + menu à esquerda, título ao centro
// e botões de controle à direita. Arrastar move a janela; duplo clique
// alterna entre maximizar e restaurar.
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    // Menus "File" e "View", para a janela acrescentar suas ações
    QMenu *fileMenu() const { return m_fileMenu; }
    QMenu *viewMenu() const { return m_viewMenu; }

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

    TitleBarParams m_params;

    QSvgWidget *m_logo = nullptr;
    QMenuBar *m_menuBar = nullptr;
    QMenu *m_fileMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QLabel *m_titleLabel = nullptr;
    TitleBarButton *m_minimizeButton = nullptr;
    TitleBarButton *m_maximizeButton = nullptr;
    TitleBarButton *m_closeButton = nullptr;

    // Estado do arraste (o movimento nativo só começa após um pequeno deslocamento)
    QPoint m_pressPos;
    bool m_dragPending = false;
};
