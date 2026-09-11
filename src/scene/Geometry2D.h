#pragma once

#include "SceneParams.h"
#include "hand/Polyline.h"

#include <QRectF>

#include <vector>

enum class LineStyle { Solid, Dashed, Dotted };

// Converte formas 2D em polilinhas, em unidades da lousa.
// Ângulos em graus: 0 = direita, positivo = sentido anti-horário (Y para baixo).
// Curvas usam amostragem adaptativa: segmentos suficientes para que a corda
// nunca se afaste mais que curveTolerance da curva verdadeira.
class Geometry2D
{
public:
    explicit Geometry2D(const SceneParams &params);

    // Começa no topo e segue no sentido anti-horário, como a mão costuma fazer
    Polyline circle(const QPointF &center, double radius) const;
    Polyline ellipse(const QPointF &center, double radiusX, double radiusY) const;
    Polyline arc(const QPointF &center, double radius, double startDeg, double endDeg) const;
    Polyline rectangle(const QPointF &center, double width, double height) const;
    // Polígono fechado com vértices relativos a `origin`
    Polyline polygon(const QPointF &origin, const std::vector<QPointF> &relativePoints) const;
    Polyline line(const QPointF &from, const QPointF &to) const;
    // Ponta de seta em `tip`, apontando na direção from → tip
    Polyline arrowHead(const QPointF &from, const QPointF &tip) const;

    // Tracejado e pontilhado quebram cada polilinha em segmentos curtos
    std::vector<Polyline> styled(const std::vector<Polyline> &lines, LineStyle style) const;

    static QRectF bounds(const std::vector<Polyline> &lines);

private:
    Polyline ellipseArc(const QPointF &center, double radiusX, double radiusY,
                        double startDeg, double sweepDeg) const;
    std::vector<Polyline> pattern(const std::vector<Polyline> &lines, double on, double off) const;

    SceneParams m_params;
};
