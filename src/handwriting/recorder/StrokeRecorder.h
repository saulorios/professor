#pragma once

#include "handwriting/HandwritingParams.h"
#include "handwriting/types/HandwritingTypes.h"

#include <QPointF>
#include <QSizeF>

#include <vector>

namespace handwriting {

// Amostra crua vinda do dispositivo (mouse, caneta ou toque)
struct RawSample {
    QPointF pos;                 // px da área de captura
    double timeMs = 0.0;         // relógio do evento (qualquer origem, crescente)
    float pressure = kUnknown;
    float tilt = kUnknown;       // graus
    PointerKind pointer = PointerKind::Unknown;
};

// Guia da captura, em px: define o espaço do glifo
struct CaptureGuide {
    double baselineY = 0.0;      // linha de base
    double unitPx = 1.0;         // da linha de base até a linha das maiúsculas
    QSizeF areaPx;
};

// Núcleo do gravador, sem widget: pointerDown → begin(), pointerMove → add(),
// pointerUp → end(). Guarda cada amostra exatamente como chegou (sem suavizar
// nem descartar pontos repetidos) e, ao mesmo tempo, a converte para o espaço
// do glifo com a guia vigente no primeiro toque do glifo (mudar a guia no meio
// da letra não desalinha os strokes).
class StrokeRecorder
{
public:
    explicit StrokeRecorder(const HandwritingParams &params = HandwritingParams());

    void setGuide(const CaptureGuide &guide) { m_guide = guide; }
    const CaptureGuide &guide() const { return m_guide; }

    void begin(const RawSample &sample);
    void add(const RawSample &sample);
    void end(const RawSample &sample);
    void end();                  // fim sem posição nova (ex.: ponteiro saiu)

    bool undo();                 // remove o último stroke terminado
    void clear();

    bool isStrokeOpen() const { return m_open; }
    bool isEmpty() const { return m_strokes.empty() && !m_open; }
    // Strokes terminados e, se houver, o que está sendo desenhado (último)
    const std::vector<Stroke> &strokes() const { return m_strokes; }

    // Monta a variante: x = 0 na borda esquerda, medidas calculadas.
    // Não altera o que foi gravado (pode ser chamado de novo).
    GlyphVariant build(const QString &character, const QString &variantId) const;

private:
    void append(const RawSample &sample);

    HandwritingParams m_params;
    CaptureGuide m_guide;
    CaptureInfo m_capture;       // guia e dispositivo no primeiro toque do glifo
    std::vector<Stroke> m_strokes;
    bool m_open = false;
    double m_glyphStart = 0.0;   // relógio do primeiro toque do glifo
    double m_strokeStart = 0.0;  // relógio do toque do stroke aberto
};

} // namespace handwriting
