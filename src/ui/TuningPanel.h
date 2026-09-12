#pragma once

#include "hand/HandParams.h"
#include "physics/PhysicsParams.h"
#include "render/ChalkOverlay.h"
#include "scene/Humanizer.h"

#include <QString>
#include <QWidget>

#include <cstddef>
#include <vector>

class QLabel;
class QSlider;

// Todos os parâmetros ajustáveis pelo painel
struct TunableParams {
    PhysicsParams physics;
    HandParams hand;
    GizParams giz;
    HumanizerParams humanizer;
};

// Parâmetros do próprio painel (px)
struct TuningPanelParams {
    int width = 340;
    int borderWidth = 1;   // borda esquerda (border-left no QSS)
    int margin = 12;
    int spacing = 6;
};

// Painel de ajuste (F10): um slider por campo de PhysicsParams e HandParams,
// com o valor atual. Cada mudança é emitida na hora; os campos que regeneram a
// superfície só são aplicados ao soltar o slider. Campos estruturais
// (resolução da lousa, px por unidade) aparecem, mas não são editáveis.
class TuningPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TuningPanel(QWidget *parent = nullptr);

    const TunableParams &values() const { return m_values; }

    // Mostra os valores nos sliders sem emitir valuesChanged()
    void setValues(const TunableParams &values);

    void setStatus(const QString &text);

    // params.json: {"physics": {...}, "hand": {...}}, com os nomes dos campos como
    // chaves. Só os campos editáveis são gravados; ao carregar, cada valor é
    // limitado à faixa do seu slider e os ausentes ficam como estão.
    static bool load(const QString &path, TunableParams *values, QString *error);
    static bool save(const QString &path, const TunableParams &values, QString *error);

signals:
    void valuesChanged(const TunableParams &values);
    void clearRequested();
    void saveRequested();

private:
    struct Row {
        QSlider *slider;
        QLabel *value;
    };

    void updateRow(std::size_t index);
    void restoreDefaults();

    TuningPanelParams m_params;
    TunableParams m_values;
    std::vector<Row> m_rows;   // na mesma ordem da tabela de campos
    QLabel *m_status = nullptr;
    bool m_updating = false;
};
