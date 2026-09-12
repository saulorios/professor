#pragma once

#include "hand/ChalkPose.h"

#include <QColor>
#include <QRectF>

class QPainter;

// Aparência do giz desenhado na tela (overlay em QPainter, fora do
// DepositBuffer: não deixa rastro). Distâncias em unidades da lousa.
struct GizParams {
    bool visible = true;         // View > "Mostrar giz"
    double length = 3.0;         // comprimento do corpo
    double bodyRadius = 0.52;    // meia largura na base
    double tipRadius = 0.36;     // meia largura na ponta (gasta)
    double tiltDegrees = 35.0;   // inclinação para trás, como uma mão segurando
    double liftHeight = 1.6;     // quanto sobe na tela quando a mão levanta
    double liftFade = 0.45;      // quanto da opacidade se perde no alto do voo
    double fadeMs = 300.0;       // desaparecimento ao pausar ou terminar
    bool shadow = false;         // sombra suave na lousa (padrão: não)
    double shadowOffset = 0.6;
    double shadowOpacity = 0.22;
    double sideWidth = 0.40;     // fração da largura ocupada pela face lateral escura
    double sideDarken = 0.68;    // quanto a face lateral escurece
    double capFlatten = 0.42;    // achatamento da elipse da base (perspectiva)
    double wear = 0.55;          // irregularidade da ponta gasta
    QColor body{0xF2, 0xF0, 0xE6};
    double pixelsPerUnit = 12.0; // preenchido pela janela a partir de HandParams
};

// Desenha o giz com a ponta em pose.tip. O painter tem de estar em pixels da
// lousa (a janela já aplicou a translação e a escala do widget).
void paintChalk(QPainter &painter, const ChalkPose &pose, const GizParams &params, double opacity);

// Retângulo ocupado pelo desenho acima, em pixels da lousa (para repintar só ele)
QRectF chalkBounds(const ChalkPose &pose, const GizParams &params);
