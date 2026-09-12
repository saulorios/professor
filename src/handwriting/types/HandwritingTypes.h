#pragma once

#include <QDateTime>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <vector>

// Banco de gestos de escrita manual: COMO cada caractere foi escrito (traços,
// ordem, tempo, pressão), e não como ele aparece. Nada aqui conhece o giz, a
// lousa ou a mão: são dados de referência que o HandwritingEngine lê e nunca
// altera (BANCO = referência, ENGINE = variação).
//
// Espaço do glifo (usado em `WritingPoint::pos` de `Stroke::points`):
//   - 1 unidade = altura da guia de captura, da linha de base até a linha das
//     maiúsculas. Não depende da resolução nem do tamanho da tela de captura.
//   - Y para baixo, como no resto do projeto; linha de base em y = 0 (as
//     maiúsculas ficam em y ≈ -1, as descendentes em y > 0).
//   - x = 0 na borda esquerda do traçado (bounds.left() == 0).
namespace handwriting {

// Valor ausente para pressão e inclinação (o mouse não informa nenhuma das duas)
constexpr float kUnknown = -1.0f;

// Linha de base do glifo, por convenção
constexpr double kBaseline = 0.0;

// Dispositivo que produziu a captura
enum class PointerKind { Unknown, Mouse, Pen, Touch };

// Uma amostra do gesto
struct WritingPoint {
    QPointF pos;                // espaço do glifo (ou px do dispositivo, em rawPoints)
    double timeMs = 0.0;        // relativo ao início do stroke (o 1º ponto é 0)
    float pressure = kUnknown;  // 0..1, ou kUnknown
    float tilt = kUnknown;      // graus a partir da vertical (0..90), ou kUnknown
    double velocity = 0.0;      // derivada (unidades/s); recalculada, não é gravada
};

// Medidas de um stroke, derivadas dos pontos (recalculadas ao carregar)
struct StrokeMetrics {
    QPointF start;
    QPointF end;
    QRectF bounds;
    double length = 0.0;            // comprimento percorrido
    double durationMs = 0.0;        // do toque até levantar
    double direction = 0.0;         // direção predominante (início → fim), radianos, Y para baixo
    double initialDirection = 0.0;  // direção de entrada do traço (útil para o pulso)
    double meanVelocity = 0.0;      // length / duração (unidades/s)
};

// Um contato contínuo do giz com a superfície: todo stroke é "pen down".
// O "pen up" é o intervalo entre o fim de um stroke e o `startMs` do seguinte.
struct Stroke {
    int id = 0;                            // ordem de escrita (0, 1, 2...)
    double startMs = 0.0;                  // início, relativo ao primeiro toque do glifo
    std::vector<WritingPoint> points;      // espaço do glifo: a referência que o motor consome
    std::vector<WritingPoint> rawPoints;   // px do dispositivo, exatamente como capturados
    StrokeMetrics metrics;                 // de `points`

    double endMs() const { return startMs + metrics.durationMs; }
};

// Medidas do glifo inteiro (espaço do glifo), derivadas dos strokes
struct GlyphMetrics {
    QRectF bounds;             // largura, altura e centro vêm daqui; baseline em y = 0
    QPointF start;             // primeiro ponto do primeiro stroke
    QPointF end;               // último ponto do último stroke
    double durationMs = 0.0;   // do primeiro toque até a última levantada
    double inkMs = 0.0;        // soma do tempo com o giz encostado
    double penUpMs = 0.0;      // soma do tempo com o giz levantado entre strokes

    double ascent() const { return kBaseline - bounds.top(); }       // acima da linha de base
    double descent() const { return qMax(0.0, bounds.bottom() - kBaseline); }
};

// Como a captura foi feita. Liga os pontos crus aos normalizados:
// points = (rawPoints − originPx) / unitPx
struct CaptureInfo {
    PointerKind pointer = PointerKind::Unknown;
    double unitPx = 1.0;      // px do dispositivo por unidade do glifo (altura da guia)
    QPointF originPx;         // px que virou (0, 0): borda esquerda do traçado na linha de base
    QSizeF areaPx;            // tamanho da área de captura
    QDateTime recordedAt;     // UTC
};

// Uma execução do gesto: A01 e A02 são a mesma letra, escrita duas vezes
struct GlyphVariant {
    QString character;        // um caractere (QString: acentos compostos e símbolos fora do BMP)
    QString variantId;        // único no banco: "A01", "a03", "U00E102"
    std::vector<Stroke> strokes;  // na ordem em que foram escritos
    GlyphMetrics metrics;
    CaptureInfo capture;
};

// Todas as variantes de um caractere, ordenadas pelo número
struct Glyph {
    QString character;
    std::vector<GlyphVariant> variants;
};

// Quanto um estilo de escrita varia em torno das variantes do banco.
// Contrato para o VariationEngine (próxima etapa): nada disso é aplicado ainda.
// Valores são amplitudes (±) relativas, salvo indicação.
struct WritingProfile {
    QString name = "padrao";
    double sizeVariation = 0.0;        // fração do tamanho
    double rotationVariation = 0.0;    // graus
    double spacingVariation = 0.0;     // fração do espaçamento entre letras
    double baselineVariation = 0.0;    // fração do tamanho
    double strokeVariation = 0.0;      // deformação da trajetória, fração do tamanho
    double pressureVariation = 0.0;    // fração da pressão
    double speedVariation = 0.0;       // fração da velocidade
};

} // namespace handwriting
