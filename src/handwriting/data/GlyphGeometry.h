#pragma once

#include "handwriting/HandwritingParams.h"
#include "handwriting/types/HandwritingTypes.h"

#include <vector>

// Medidas derivadas dos pontos. Os pontos são a fonte da verdade: métricas e
// velocidades são sempre recalculadas a partir deles (ao capturar e ao carregar).
namespace handwriting::geometry {

// Velocidade de cada ponto por diferença central (unidades do próprio vetor / s)
void computeVelocities(std::vector<WritingPoint> &points);

// Medidas de um stroke a partir de `points`
StrokeMetrics strokeMetrics(const std::vector<WritingPoint> &points,
                            const HandwritingParams &params = HandwritingParams());

// Recalcula velocidades e medidas de todos os strokes e do glifo
void updateDerived(GlyphVariant &variant, const HandwritingParams &params = HandwritingParams());

// Leva o traçado para x = 0 na borda esquerda, sem mexer em Y (a linha de base
// já é y = 0) nem nos pontos crus: só `originPx` acompanha, mantendo
// points = (raw − originPx) / unitPx. Recalcula as medidas.
void normalizeLeftEdge(GlyphVariant &variant, const HandwritingParams &params = HandwritingParams());

} // namespace handwriting::geometry
