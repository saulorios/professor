#pragma once

#include "ChalkOverlay.h"
#include "hand/ChalkPose.h"
#include "physics/Board.h"
#include "physics/Eraser.h"
#include "physics/StrokeEngine.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <vector>

class QLabel;
class QPushButton;
class QScrollBar;
class QTabletEvent;

// Parâmetros ajustáveis da lousa na tela
struct BoardCanvasParams {
    float mousePressure = 0.6f;       // o mouse não tem pressão real
    float mouseTilt = 0.0f;
    float tiltStartDegrees = 25.0f;   // inclinação natural da caneta: ainda é "giz de ponta"
    float tiltFullDegrees = 60.0f;    // a partir daqui é "giz deitado" (tilt = 1)
    float boardVariation = 0.08f;     // variação de brilho da lousa pelo height map (±)
    QColor boardColor{0x1E, 0x26, 0x21};
    QColor chalkColor{0xF2, 0xF0, 0xE6};

    // Rolagem
    int scrollAnimationMs = 600;      // uma tela inteira leva isto (rolagem automática)
    double wheelStep = 90.0;          // px do canvas por "clique" da roda
    double revealMargin = 24.0;       // folga ao trazer algo para a vista (px do canvas)
    int scrollBarWidth = 8;

    // Modo de depuração (F12)
    QColor debugBoxColor{0x4F, 0xC1, 0xFF};   // bounding boxes e ids
    QColor debugAreaColor{0xD7, 0xBA, 0x7D};  // contorno da área útil
    QColor debugHiddenColor{0xC5, 0x86, 0xC0};    // arestas ocultas dos objetos 3D
    QColor debugVanishingColor{0x6A, 0x99, 0x55}; // pontos e linhas de fuga
    int debugFontPx = 11;
    int debugPointRadius = 4;
};

// Retângulo do modo de depuração, em pixels do canvas
struct OverlayBox {
    QRectF rect;
    QString label;
    bool area = false;   // contorno da área útil (tracejado)
};

// Linha do modo de depuração, em pixels do canvas: aresta oculta de um objeto 3D
// ou linha fina até um ponto de fuga
struct OverlayLine {
    QLineF line;
    bool vanishing = false;
};

// Lousa: converte mouse e mesa digitalizadora em ChalkSamples, alimenta a
// física e exibe o DepositBuffer, atualizando apenas a região alterada.
// Botão esquerdo (ou ponta da caneta) = giz; botão direito = apagador.
//
// O canvas é mais alto que a tela e rola: esta classe mostra só a faixa visível
// (`scroll()` em pixels do canvas) e converte as coordenadas do mouse para o
// canvas. A rolagem do usuário é imediata; a do motor é animada.
class BoardCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit BoardCanvas(Board &board, QWidget *parent = nullptr);

    bool isOverlayVisible() const { return m_overlayVisible; }

    const GizParams &gizParams() const { return m_giz; }
    void setGizParams(const GizParams &params);

    double scroll() const { return m_scroll; }
    int screenCount() const;
    int currentScreen() const;

public slots:
    // Limpa a lousa e troca o giz por um novo
    void clear();

    // Recalcula no cache apenas a região alterada da física e agenda o repaint dela
    void refresh();

    // Legenda na parte inferior da tela; texto vazio esconde a legenda.
    // A faixa da legenda é overlay da TELA: não faz parte do canvas.
    void setCaption(const QString &text);
    void setCaptionBand(double screenPixels);

    // Recompõe o fundo depois que a superfície da lousa foi regenerada
    void rebuildSurface();

    // O canvas cresceu ou voltou a uma tela só
    void canvasChanged();

    // Aula nova: o motor volta a mandar na vista (desfaz a rolagem manual)
    void resumeFollowing();

    // Rolagem imediata (usuário) e animada (motor)
    void setScroll(double canvasPixels);
    void scrollBy(double canvasPixels);
    void scrollToScreen(int screen, bool animated);
    // O motor pede para mostrar esta faixa do canvas (px). Se o usuário rolou
    // por conta própria, em vez de arrastar a vista mostra o aviso clicável.
    void followTo(const QRectF &canvasRect);

    // Modo de depuração: retângulos e rótulos desenhados por cima da lousa com
    // QPainter, fora do DepositBuffer
    void setOverlay(const std::vector<OverlayBox> &boxes);
    // Linhas e pontos extras dos objetos 3D (arestas ocultas e pontos de fuga)
    void setOverlayGeometry(const std::vector<OverlayLine> &lines, const std::vector<QPointF> &points);
    void setOverlayVisible(bool visible);

    // Giz da mão virtual: aparece na posição da amostra atual e some com fade.
    // O giz do mouse não usa isto (o cursor do usuário já faz esse papel).
    void setChalkPose(const ChalkPose &pose);
    void hideChalk();

    // Indicador discreto de gravação, no canto da lousa
    void setRecording(bool on);

signals:
    // Traços feitos à mão pelo usuário (só o giz), para o gravador de aulas
    void freeStrokeStarted();
    void freeSample(const QPointF &canvasPixels, float pressure, double timeMs);
    void freeStrokeFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;

private:
    enum class Tool { None, Chalk, Eraser };

    // Área (em coordenadas do widget) onde a tela 16:9 é exibida
    QRectF boardRect() const;
    // Widget → canvas (já com a rolagem somada) e canvas → widget
    QPointF toCanvas(const QPointF &widgetPos) const;
    QRect toWidget(const QRect &canvasArea) const;

    ChalkSample mouseSample(const QMouseEvent *event) const;
    float tabletTilt(const QTabletEvent *event) const;

    void beginTool(Tool tool, const ChalkSample &sample);
    void moveTool(const ChalkSample &sample);
    void endTool();

    double maxScroll() const;
    void applyScroll(double canvasPixels, bool fromUser);
    void animateTo(double canvasPixels);
    void composeView(const QRect &canvasArea);
    void rebuildView();
    void updateChrome();          // barra de rolagem, indicador e aviso
    void updateCaptionGeometry();
    // Repinta a área do giz (a de antes e a de agora), em coordenadas do widget
    void refreshChalk(const ChalkPose &previous, bool hadChalk);

    BoardCanvasParams m_params;
    Board &m_board;
    StrokeEngine m_stroke; // traços do mouse / caneta
    Eraser m_eraser;

    QImage m_view;   // faixa visível já composta (largura do canvas × altura da tela)
    int m_viewTop = 0;      // linha do canvas que está no topo de m_view
    double m_scroll = 0.0;  // px do canvas no topo da tela
    bool m_userScrolled = false;   // o usuário assumiu o controle da rolagem
    QRectF m_pending;              // o que o motor quer mostrar e não está à vista

    QTimer m_scrollTimer;          // animação da rolagem automática
    QElapsedTimer m_scrollClock;
    double m_scrollFrom = 0.0;
    double m_scrollTo = 0.0;
    int m_scrollDuration = 0;

    Tool m_tool = Tool::None;
    QLabel *m_caption = nullptr;
    QLabel *m_recording = nullptr;
    QLabel *m_position = nullptr;      // indicador "2/3"
    QPushButton *m_below = nullptr;    // aviso "continuando abaixo ↓"
    QScrollBar *m_scrollBar = nullptr;
    double m_captionBandPx = 0.0;   // px da tela

    GizParams m_giz;
    ChalkPose m_chalkPose;
    bool m_chalkVisible = false;
    QTimer m_fadeTimer;        // desaparecimento do giz
    QElapsedTimer m_fadeClock;
    bool m_fading = false;

    std::vector<OverlayBox> m_overlay;
    std::vector<OverlayLine> m_overlayLines;
    std::vector<QPointF> m_overlayPoints;
    bool m_overlayVisible = false;
};
