#pragma once

#include "Expression.h"
#include "hand/Polyline.h"

#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

// Contas do comando "grafico", sem nada de desenho: faixa dos eixos, marcações
// e a curva já cortada na área do gráfico. Tudo no espaço do gráfico: o retângulo
// `plot` (unidades da lousa, Y para baixo) corresponde a [xMin, xMax] × [yMin, yMax].
namespace chart {

struct Frame {
    double xMin = -5.0, xMax = 5.0;
    double yMin = -5.0, yMax = 5.0;
    QRectF plot;   // onde a faixa acima aparece na lousa

    QPointF map(double x, double y) const;
};

// Marcações com passo 1, 2 ou 5 × 10^n, cerca de `target` delas dentro da faixa
double niceStep(double min, double max, int target);
std::vector<double> ticks(double min, double max, double step);

// Faixa de y que mostra as funções em [xMin, xMax]: descarta os extremos (1/x
// perto de 0), inclui o zero quando ele está por perto e deixa uma folga
// pequena. false se nenhuma função tem valor definido na faixa.
bool autoRange(const std::vector<const Expression *> &functions, double xMin, double xMax, int samples,
               double *yMin, double *yMax);

// A curva em trechos: interrompida onde a função não existe (NaN), onde sai da
// faixa de y (cortada exatamente na borda) e nos saltos de assíntota
std::vector<Polyline> sample(const Expression &function, const Frame &frame, int samples, double jumpFraction);

// Número como se escreve na lousa: vírgula decimal, sem zeros sobrando
QString format(double value, double step);

} // namespace chart
