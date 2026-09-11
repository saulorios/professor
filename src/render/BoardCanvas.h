#pragma once

#include "physics/Board.h"
#include "physics/Eraser.h"
#include "physics/StrokeEngine.h"

#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
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

    // Modo de depuração (F12)
    QColor debugBoxColor{0x4F, 0xC1, 0xFF};   // bounding boxes e ids
    QColor debugAreaColor{0xD7, 0xBA, 0x7D};  // contorno da área útil
    int debugFontPx = 11;
};

// Retângulo do modo de depuração, em pixels da lousa
struct OverlayBox {
    QRectF rect;
    QString label;
    bool area = false;   // contorno da área útil (tracejado)
};

// Lousa: converte mouse e mesa digitalizadora em ChalkSamples, alimenta a
// física e exibe o DepositBuffer, atualizando apenas a região alterada.
// Botão esquerdo (ou ponta da caneta) = giz; botão direito = apagador.
class BoardCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit BoardCanvas(Board &board, QWidget *parent = nullptr);

    bool isOverlayVisible() const { return m_overlayVisible; }

public slots:
    // Limpa a lousa e troca o giz por um novo
    void clear();

    // Recalcula no cache apenas a região alterada da física e agenda o repaint dela
    void refresh();

    // Legenda na parte inferior da lousa; texto vazio esconde a legenda
    void setCaption(const QString &text);

    // Altura (px da lousa) da faixa reservada à legenda; a legenda ocupa ao menos isso
    void setCaptionBand(double boardPixels);

    // Recompõe o fundo depois que a superfície da lousa foi regenerada
    void rebuildSurface();

    // Modo de depuração: retângulos e rótulos desenhados por cima da lousa com
    // QPainter, fora do DepositBuffer
    void setOverlay(const std::vector<OverlayBox> &boxes);
    void setOverlayVisible(bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;

private:
    enum class Tool { None, Chalk, Eraser };

    // Área (em coordenadas do widget) onde a lousa 16:9 é exibida
    QRectF boardRect() const;
    QPointF toBoard(const QPointF &widgetPos) const;
    QRect toWidget(const QRect &boardArea) const;

    ChalkSample mouseSample(const QMouseEvent *event) const;
    float tabletTilt(const QTabletEvent *event) const;

    void beginTool(Tool tool, const ChalkSample &sample);
    void moveTool(const ChalkSample &sample);
    void endTool();

    void buildBaseImage();
    void updateCaptionGeometry();

    BoardCanvasParams m_params;
    Board &m_board;
    StrokeEngine m_stroke; // traços do mouse / caneta
    Eraser m_eraser;

    QImage m_base;   // cor da lousa já variada pelo height map
    QImage m_image;  // cache exibido na tela
    Tool m_tool = Tool::None;
    QLabel *m_caption = nullptr;
    double m_captionBandPx = 0.0;   // px da lousa

    std::vector<OverlayBox> m_overlay;
    bool m_overlayVisible = false;
};
