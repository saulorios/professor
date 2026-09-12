#include "ChalkOverlay.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

constexpr double kPi = 3.141592653589793;

// Eixo do giz: sai da ponta para trás, inclinado como na mão de quem escreve
QPointF axisOf(const ChalkPose &pose, const GizParams &params)
{
    const double angle = pose.heading + kPi + params.tiltDegrees * kPi / 180.0;
    return QPointF(std::cos(angle), std::sin(angle));
}

QColor darker(const QColor &color, double factor)
{
    return QColor(int(color.red() * factor), int(color.green() * factor), int(color.blue() * factor));
}

} // namespace

QRectF chalkBounds(const ChalkPose &pose, const GizParams &params)
{
    const double ppu = params.pixelsPerUnit;
    const double reach = (params.length + params.bodyRadius + params.liftHeight + params.shadowOffset + 1.0) * ppu;
    return QRectF(pose.tip - QPointF(reach, reach), QSizeF(2 * reach, 2 * reach));
}

void paintChalk(QPainter &painter, const ChalkPose &pose, const GizParams &params, double opacity)
{
    if (opacity <= 0.0)
        return;
    const double ppu = params.pixelsPerUnit;
    const QPointF axis = axisOf(pose, params);
    const QPointF side(-axis.y(), axis.x()); // perpendicular ao eixo

    // No ar o giz sobe um pouco e clareia
    const QPointF tip = pose.tip - QPointF(0.0, pose.lift * params.liftHeight * ppu);
    const double alpha = opacity * (1.0 - params.liftFade * pose.lift);
    const QPointF back = tip + axis * (params.length * ppu);
    const double tipHalf = params.tipRadius * ppu;
    const double backHalf = params.bodyRadius * ppu;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); // giz não tem contorno

    if (params.shadow) {
        // Sombra suave, sem borda dura
        painter.setOpacity(alpha * params.shadowOpacity);
        painter.setBrush(QColor(0, 0, 0));
        QPainterPath sombra;
        const QPointF desvio(0.0, params.shadowOffset * ppu);
        sombra.moveTo(tip + desvio - side * tipHalf);
        sombra.lineTo(back + desvio - side * backHalf);
        sombra.lineTo(back + desvio + side * backHalf);
        sombra.lineTo(tip + desvio + side * tipHalf);
        sombra.closeSubpath();
        painter.drawPath(sombra);
    }

    painter.setOpacity(alpha);

    // Corpo: tronco de cone da ponta até a base, com a base fechada por uma
    // elipse achatada (a perspectiva simples de um cilindro visto de lado).
    // A frente é irregular, de giz gasto, mas passa exatamente pela amostra.
    const double wear = params.wear * tipHalf;
    QPainterPath corpo;
    corpo.moveTo(tip - side * tipHalf + axis * (0.35 * wear));
    corpo.lineTo(tip - side * (0.45 * tipHalf) + axis * (0.15 * wear));
    corpo.lineTo(tip); // a ponta do giz é a posição atual da mão
    corpo.lineTo(tip + side * (0.5 * tipHalf) + axis * (0.5 * wear));
    corpo.lineTo(tip + side * tipHalf + axis * (0.2 * wear));
    corpo.lineTo(back + side * backHalf);
    corpo.lineTo(back - side * backHalf);
    corpo.closeSubpath();
    painter.setBrush(params.body);
    painter.drawPath(corpo);

    QPainterPath tampa;
    tampa.addEllipse(QPointF(0, 0), backHalf, backHalf * params.capFlatten);
    painter.save();
    painter.translate(back);
    painter.rotate(std::atan2(side.y(), side.x()) * 180.0 / kPi);
    painter.setBrush(params.body.lighter(104));
    painter.drawPath(tampa);
    painter.restore();

    // Face lateral mais escura, do lado de baixo do cilindro
    QPainterPath lateral;
    const double tipDark = tipHalf * params.sideWidth * 2.0;
    const double backDark = backHalf * params.sideWidth * 2.0;
    lateral.moveTo(tip + side * (tipHalf - tipDark) + axis * (0.35 * wear));
    lateral.lineTo(back + side * (backHalf - backDark));
    lateral.lineTo(back + side * backHalf);
    lateral.lineTo(tip + side * tipHalf + axis * (0.2 * wear));
    lateral.closeSubpath();
    painter.setBrush(darker(params.body, params.sideDarken));
    painter.drawPath(lateral);

    painter.restore();
}
