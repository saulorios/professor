#pragma once

#include "handwriting/recorder/StrokeRecorder.h"
#include "handwriting/types/HandwritingTypes.h"

#include <QColor>
#include <QElapsedTimer>
#include <QWidget>

#include <optional>

class QPointerEvent;

// Parâmetros da área de captura (frações da altura do widget)
struct HandwritingCaptureParams {
    double guideHeight = 0.42;      // da linha de base à linha das maiúsculas
    double baseline = 0.68;         // posição da linha de base
    double xHeight = 0.55;          // linha auxiliar das minúsculas (fração da guia)
    double descender = 0.35;        // linha auxiliar das descendentes (fração da guia)
    double strokeWidth = 3.0;       // px
    double pointRadius = 2.5;       // px
    double arrowSize = 9.0;         // px
    double labelOffset = 12.0;      // número do stroke, afastado do ponto inicial
    double previewLetter = 0.9;     // letra de referência ao fundo (fração da guia)
    QColor background{0x1E, 0x26, 0x21};
    QColor guide{0x4A, 0x55, 0x4E};
    QColor ink{0xF2, 0xF0, 0xE6};
    QColor penUp{0xD7, 0xBA, 0x7D};
    QColor points{0x4F, 0xC1, 0xFF};
    QColor ghost{0x2C, 0x36, 0x30};
};

// Área onde a letra é escrita. Mouse, caneta (com pressão e inclinação) e toque
// viram pointerDown/Move/Up do StrokeRecorder, com o timestamp do próprio
// evento. Também mostra uma variante carregada do banco, sem editá-la.
class HandwritingCapture : public QWidget
{
    Q_OBJECT

public:
    explicit HandwritingCapture(QWidget *parent = nullptr);

    handwriting::StrokeRecorder &recorder() { return m_recorder; }

    void setCharacter(const QString &character);   // letra-guia ao fundo
    void showVariant(const handwriting::GlyphVariant &variant);
    bool isShowingVariant() const { return m_variant.has_value(); }

    void clear();
    bool undo();

    void setShowTrajectory(bool on);
    void setShowPoints(bool on);

signals:
    void strokesChanged();     // stroke terminado, desfeito ou limpo
    void captureStarted();     // um toque novo tirou a variante carregada da tela

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class Phase { Down, Move, Up };

    void handlePointer(QPointerEvent *event, Phase phase, handwriting::PointerKind kind);
    void updateGuide();
    double baselineY() const;
    double unitPx() const;
    double eventTime(const QPointerEvent *event);

    HandwritingCaptureParams m_params;
    handwriting::StrokeRecorder m_recorder;
    std::optional<handwriting::GlyphVariant> m_variant;
    QString m_character;
    bool m_showTrajectory = true;
    bool m_showPoints = false;
    double m_lastTimeMs = 0.0;      // último instante entregue ao gravador
    QElapsedTimer m_sinceLast;      // para eventos sem timestamp (sintéticos)
};
