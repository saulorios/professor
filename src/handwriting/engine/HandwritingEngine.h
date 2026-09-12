#pragma once

#include "handwriting/HandwritingParams.h"
#include "handwriting/types/HandwritingTypes.h"
#include "handwriting/types/WritingTrajectory.h"

#include <QPointF>
#include <QString>

#include <cstdint>

namespace handwriting {

class GlyphDatabase;

// Como UMA variante vai para a lousa. É aqui que a variação entra: a variante
// do banco nunca é modificada, só lida através desta transformação.
// Valores neutros = a variante exatamente como foi capturada.
struct GlyphPlacement {
    QPointF origin;              // borda esquerda do glifo na linha de base (unidades da lousa)
    double size = 4.0;           // unidades da lousa por unidade do glifo (altura da guia)
    double scale = 1.0;          // multiplica `size`
    double rotationDegrees = 0.0;  // em torno de `origin`, positivo = anti-horário
    double baselineOffset = 0.0; // deslocamento vertical (unidades da lousa, Y para baixo)
    double speed = 1.0;          // > 1 escreve mais rápido (o tempo encolhe)
    float pressureScale = 1.0f;
};

struct HandwritingOptions {
    QPointF origin;              // começo da linha de base
    double size = 4.0;           // altura da guia (≈ maiúsculas), como o "tamanho" do texto
    double speed = 1.0;
    std::uint32_t seed = 0;      // escolha determinística das variantes
    WritingProfile profile;      // reservado ao VariationEngine: ignorado nesta etapa
};

// Texto → WritingTrajectory, usando as variantes do banco.
// Etapa 1: escolhe uma variante por caractere (pela seed), posiciona numa linha
// só, sem variação nem quebra de linha. O contrato (entrada e saída) é o que o
// VariationEngine e o HumanMotionEngine vão usar.
class HandwritingEngine
{
public:
    explicit HandwritingEngine(const GlyphDatabase &database, const HandwritingParams &params = HandwritingParams());

    WritingTrajectory generate(const QString &text, const HandwritingOptions &options) const;

    // Acrescenta uma variante à trajetória, começando em `startMs`, com um
    // trecho de giz levantado desde o fim do que já havia. Devolve o avanço
    // horizontal (largura do glifo na lousa).
    static double appendGlyph(WritingTrajectory &trajectory, const GlyphVariant &variant,
                              const GlyphPlacement &placement, double startMs);

private:
    const GlyphDatabase &m_database;
    HandwritingParams m_params;
};

} // namespace handwriting
