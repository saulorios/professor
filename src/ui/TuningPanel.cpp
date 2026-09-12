#include "TuningPanel.h"

#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>

namespace {

enum FieldFlags : unsigned {
    Expensive = 1u, // regenera a superfície: aplica ao soltar o slider
    ReadOnly = 2u,  // estrutural: só exibido
};

// Um campo ajustável: faixa do slider, casas decimais e acesso ao valor
struct Field {
    const char *group;     // seção no params.json ("physics" ou "hand")
    const char *section;   // título da seção no painel
    const char *key;       // nome do campo (= chave no params.json)
    const char *label;     // descrição no painel
    double min;
    double max;
    int decimals;
    unsigned flags;
    std::function<double(const TunableParams &)> get;
    std::function<void(TunableParams &, double)> set;
};

template <typename T>
void assign(T &target, double value)
{
    if constexpr (std::is_integral_v<T>)
        target = static_cast<T>(std::llround(value));
    else
        target = static_cast<T>(value);
}

template <typename S, typename T>
Field makeField(S TunableParams::*group, const char *groupKey, const char *section, T S::*member,
                const char *key, const char *label, double min, double max, int decimals, unsigned flags)
{
    return {groupKey, section, key, label, min, max, decimals, flags,
            [group, member](const TunableParams &p) { return static_cast<double>(p.*group.*member); },
            [group, member](TunableParams &p, double v) { assign(p.*group.*member, v); }};
}

// A chave no params.json é o próprio nome do campo
#define PHYS(section, name, label, min, max, decimals, flags) \
    makeField(&TunableParams::physics, "physics", section, &PhysicsParams::name, #name, label, min, max, decimals, flags)
#define HAND(section, name, label, min, max, decimals, flags) \
    makeField(&TunableParams::hand, "hand", section, &HandParams::name, #name, label, min, max, decimals, flags)
#define GIZ(section, name, label, min, max, decimals, flags) \
    makeField(&TunableParams::giz, "giz", section, &GizParams::name, #name, label, min, max, decimals, flags)
#define HUM(section, name, label, min, max, decimals, flags) \
    makeField(&TunableParams::humanizer, "humanizer", section, &HumanizerParams::name, #name, label, min, max, decimals, flags)

// Todos os campos de PhysicsParams e HandParams, na ordem dos structs
const std::vector<Field> &fields()
{
    static const std::vector<Field> table = {
        PHYS("Física · Lousa", boardWidth, "Largura (px, fixa)", 1920, 1920, 0, ReadOnly),
        PHYS("Física · Lousa", boardHeight, "Altura (px, fixa)", 1080, 1080, 0, ReadOnly),

        PHYS("Física · Superfície", surfaceSeed, "Seed", 0, 9999, 0, Expensive),
        PHYS("Física · Superfície", surfaceFineScale, "Grão fino (px)", 0.5, 8, 2, Expensive),
        PHYS("Física · Superfície", surfaceWaveScale, "Ondulação (px)", 5, 150, 1, Expensive),
        PHYS("Física · Superfície", surfaceFineWeight, "Peso do grão fino", 0.05, 1, 2, Expensive),
        PHYS("Física · Superfície", surfaceWaveWeight, "Peso da ondulação", 0.05, 1, 2, Expensive),
        PHYS("Física · Superfície", surfaceGamma, "Gamma da altura", 0.5, 3, 2, Expensive),

        PHYS("Física · Giz", tipRadius, "Raio da ponta (px)", 1, 8, 1, 0),
        PHYS("Física · Giz", maxTipRadius, "Raio máximo com desgaste (px)", 1, 10, 1, 0),
        PHYS("Física · Giz", wearRate, "Taxa de desgaste", 0, 0.0001, 7, 0),
        PHYS("Física · Giz", efficiency, "Eficiência de depósito", 0, 1, 2, 0),
        PHYS("Física · Giz", tiltStretch, "Alongamento com tilt", 0, 5, 2, 0),

        PHYS("Física · Traço", kVelocity, "kVelocity (ms/px)", 0, 5, 2, 0),
        PHYS("Física · Traço", substepSpacing, "Espaço entre sub-passos (px)", 0.1, 2, 2, 0),
        PHYS("Física · Traço", velocitySmoothing, "Suavização da velocidade", 0.05, 1, 2, 0),
        PHYS("Física · Traço", minSampleIntervalMs, "Intervalo mínimo entre amostras (ms)", 0.1, 10, 1, 0),
        PHYS("Física · Traço", noiseBase, "Ruído: base", 0, 1, 2, 0),
        PHYS("Física · Traço", noiseAmount, "Ruído: amplitude", 0, 1, 2, 0),
        PHYS("Física · Traço", noiseSeed, "Ruído: seed", 0, 9999, 0, 0),

        PHYS("Física · Apagador", eraserRadius, "Raio (px)", 5, 60, 0, 0),
        PHYS("Física · Apagador", eraserStrength, "Força (fração removida)", 0, 1, 2, 0),
        PHYS("Física · Apagador", eraserSpread, "Espalhamento", 0, 1, 2, 0),
        PHYS("Física · Apagador", eraserNoise, "Irregularidade", 0, 1, 2, 0),
        PHYS("Física · Apagador", eraserSpacing, "Espaço entre aplicações (px)", 0.5, 10, 1, 0),
        PHYS("Física · Apagador", eraserReversalDistance, "Distância p/ medir sentido (px)", 1, 50, 0, 0),
        PHYS("Física · Apagador", eraserReversalCos, "Cosseno de nova passada", -1, 1, 2, 0),

        HAND("Mão · Conversão", pixelsPerUnit, "Px por unidade (fixo)", 12, 12, 0, ReadOnly),

        HAND("Mão · Velocidade", baseSpeed, "Velocidade de cruzeiro (u/s)", 5, 100, 0, 0),
        HAND("Mão · Velocidade", acceleration, "Aceleração (u/s²)", 20, 1000, 0, 0),
        HAND("Mão · Velocidade", startSpeed, "Velocidade no toque (u/s)", 0.5, 20, 1, 0),
        HAND("Mão · Velocidade", endSpeed, "Velocidade ao levantar (u/s)", 0.5, 20, 1, 0),
        HAND("Mão · Velocidade", curveSlowdown, "Freio nas curvas", 0, 3, 2, 0),
        HAND("Mão · Velocidade", minCurveSpeed, "Velocidade mínima nas quinas (u/s)", 0.5, 30, 1, 0),
        HAND("Mão · Velocidade", sampleSpacing, "Espaço entre amostras (u)", 0.05, 1, 2, 0),

        HAND("Mão · Escrita", writingSpeed, "Velocidade ao escrever (u/s)", 5, 120, 0, 0),
        HAND("Mão · Escrita", writingPenLiftMs, "Levantar o giz entre letras (ms)", 0, 500, 0, 0),

        HAND("Mão · Pressão", pressureLight, "Pressão \"leve\"", 0, 1, 2, 0),
        HAND("Mão · Pressão", pressureNormal, "Pressão \"normal\"", 0, 1, 2, 0),
        HAND("Mão · Pressão", pressureStrong, "Pressão \"forte\"", 0, 1, 2, 0),
        HAND("Mão · Pressão", pressureStart, "Fração no toque", 0, 1, 2, 0),
        HAND("Mão · Pressão", pressureEnd, "Fração ao levantar", 0, 1, 2, 0),
        HAND("Mão · Pressão", pressureRampIn, "Rampa de entrada (u)", 0, 10, 1, 0),
        HAND("Mão · Pressão", pressureRampOut, "Rampa de saída (u)", 0, 10, 1, 0),
        HAND("Mão · Pressão", pressureVariation, "Variação lenta (±)", 0, 0.5, 2, 0),
        HAND("Mão · Pressão", pressureWavelength, "Comprimento da variação (u)", 1, 50, 0, 0),
        HAND("Mão · Pressão", tilt, "Inclinação do giz", 0, 1, 2, 0),

        HAND("Mão · Tremor", wobbleMin, "Amplitude mínima (u)", 0, 1, 2, 0),
        HAND("Mão · Tremor", wobbleMax, "Amplitude máxima (u)", 0, 1, 2, 0),
        HAND("Mão · Tremor", wobbleWavelength, "Comprimento de onda (u)", 1, 40, 1, 0),
        HAND("Mão · Tremor", wobbleSeed, "Seed", 0, 9999, 0, 0),

        HAND("Mão · Entre traços", closedOvershoot, "Passar do início em traço fechado (u)", 0, 2, 2, 0),
        HAND("Mão · Entre traços", penLiftMs, "Levantar o giz (ms)", 0, 500, 0, 0),
        HAND("Mão · Entre traços", travelSpeed, "Deslocamento no ar (u/s)", 20, 500, 0, 0),

        HAND("Mão · Apagador", eraserSpeed, "Velocidade (u/s)", 10, 200, 0, 0),
        HAND("Mão · Apagador", eraserRowSpacing, "Espaço entre linhas do zigue-zague (u)", 0.5, 8, 1, 0),
        HAND("Mão · Apagador", eraserMargin, "Margem além da área (u)", 0, 5, 1, 0),

        HAND("Mão · Relógio", tickIntervalMs, "Intervalo do timer (ms)", 5, 50, 0, 0),
        HAND("Mão · Relógio", chalkTurnRate, "Giro do giz por tick", 0.02, 1, 2, 0),

        GIZ("Giz visível", visible, "Mostrar o giz", 0, 1, 0, 0),
        GIZ("Giz visível", length, "Comprimento (u)", 1, 8, 1, 0),
        GIZ("Giz visível", bodyRadius, "Meia largura na base (u)", 0.1, 1.5, 2, 0),
        GIZ("Giz visível", tipRadius, "Meia largura na ponta (u)", 0.05, 1, 2, 0),
        GIZ("Giz visível", tiltDegrees, "Inclinação para trás (graus)", 0, 80, 0, 0),
        GIZ("Giz visível", liftHeight, "Subida entre traços (u)", 0, 6, 1, 0),
        GIZ("Giz visível", liftFade, "Transparência no alto", 0, 1, 2, 0),
        GIZ("Giz visível", fadeMs, "Desaparecer em (ms)", 0, 2000, 0, 0),
        GIZ("Giz visível", shadow, "Sombra na lousa", 0, 1, 0, 0),
        GIZ("Giz visível", shadowOffset, "Deslocamento da sombra (u)", 0, 3, 1, 0),
        GIZ("Giz visível", shadowOpacity, "Opacidade da sombra", 0, 1, 2, 0),
        GIZ("Giz visível", sideWidth, "Largura da face lateral", 0, 1, 2, 0),
        GIZ("Giz visível", sideDarken, "Escurecimento da lateral", 0.2, 1, 2, 0),
        GIZ("Giz visível", capFlatten, "Achatamento da base", 0.05, 1, 2, 0),
        GIZ("Giz visível", wear, "Desgaste da ponta", 0, 1, 2, 0),

        HUM("Escrita humana", intensity, "Intensidade (mestre)", 0, 1, 2, 0),
        HUM("Escrita humana", scaleJitter, "Variação de tamanho (±)", 0, 0.2, 3, 0),
        HUM("Escrita humana", rotationDegrees, "Rotação da letra (± graus)", 0, 10, 1, 0),
        HUM("Escrita humana", baselineJitter, "Ondulação da linha de base (± u)", 0, 1, 2, 0),
        HUM("Escrita humana", baselineWavelength, "Comprimento da ondulação (letras)", 1, 12, 1, 0),
        HUM("Escrita humana", bowing, "Curvatura dos retos (fração)", 0, 0.1, 3, 0),
        HUM("Escrita humana", minSegment, "Reto mínimo para encurvar (u)", 0.1, 3, 2, 0),
        HUM("Escrita humana", bowSegments, "Pedaços da curva", 2, 16, 0, 0),
        HUM("Escrita humana", cornerRadius, "Arredondamento das quinas (u)", 0, 1, 2, 0),
        HUM("Escrita humana", closeGap, "Falha no fecho (fração)", 0, 0.1, 3, 0),
        HUM("Escrita humana", overshoot, "Extrapolação (fração)", 0, 0.1, 3, 0),
        HUM("Escrita humana", speedJitter, "Velocidade entre letras (±)", 0, 0.5, 2, 0),
        HUM("Escrita humana", quickChance, "Chance de traço rápido", 0, 1, 2, 0),
        HUM("Escrita humana", quickSpeed, "Quanto o traço rápido acelera", 1, 2, 2, 0),
        HUM("Escrita humana", quickPressure, "Pressão do traço rápido", 0.3, 1, 2, 0),
        HUM("Escrita humana", pauseWord, "Pausa entre palavras (ms)", 0, 600, 0, 0),
        HUM("Escrita humana", pauseComma, "Pausa após vírgula (ms)", 0, 600, 0, 0),
        HUM("Escrita humana", pauseStop, "Pausa após ponto (ms)", 0, 900, 0, 0),
        HUM("Escrita humana", cursiveScale, "Intensidade na cursiva", 0, 1, 2, 0),
        HUM("Escrita humana", seed, "Seed da aula", 0, 9999, 0, 0),
    };
    return table;
}

#undef PHYS
#undef HAND
#undef GIZ
#undef HUM

double scaleOf(const Field &field)
{
    return std::pow(10.0, field.decimals);
}

int stepsOf(const Field &field)
{
    return static_cast<int>(std::llround((field.max - field.min) * scaleOf(field)));
}

double valueAt(const Field &field, int position)
{
    return field.min + position / scaleOf(field);
}

int positionOf(const Field &field, double value)
{
    const auto position = std::llround((value - field.min) * scaleOf(field));
    return static_cast<int>(std::clamp<long long>(position, 0, stepsOf(field)));
}

QString format(const Field &field, double value)
{
    return field.decimals == 0 ? QString::number(std::llround(value)) : QString::number(value, 'f', field.decimals);
}

// Slider que só usa a roda do mouse quando focado: rolar o painel não muda valores
class FieldSlider : public QSlider
{
public:
    explicit FieldSlider(QWidget *parent)
        : QSlider(Qt::Horizontal, parent)
    {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        if (hasFocus())
            QSlider::wheelEvent(event);
        else
            event->ignore();
    }
};

QPushButton *makeButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

TuningPanel::TuningPanel(QWidget *parent)
    : QWidget(parent)
{
    // Necessário para que o QSS (fundo e borda) seja aplicado a uma subclasse de QWidget
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(m_params.width);
    const int m = m_params.margin;

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(m_params.borderWidth, 0, 0, 0);
    outer->setSpacing(0);

    auto *title = new QLabel("Ajustes (F10)", this);
    title->setObjectName("TuningTitle");
    title->setContentsMargins(m, m, m, m / 2);
    outer->addWidget(title);

    // Um slider por campo, agrupados por seção, dentro de uma área rolável
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    content->setObjectName("TuningContent");
    auto *grid = new QGridLayout(content);
    grid->setContentsMargins(m, 0, m, m);
    grid->setHorizontalSpacing(m_params.spacing);
    grid->setVerticalSpacing(2);

    int row = 0;
    const char *section = nullptr;
    for (std::size_t i = 0; i < fields().size(); ++i) {
        const Field &field = fields()[i];
        if (!section || qstrcmp(section, field.section) != 0) {
            section = field.section;
            auto *header = new QLabel(QString::fromUtf8(field.section), content);
            header->setObjectName("TuningSection");
            grid->addWidget(header, row++, 0, 1, 2);
        }

        auto *name = new QLabel(QString::fromUtf8(field.label), content);
        name->setToolTip(QString::fromLatin1(field.key));
        auto *value = new QLabel(content);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *slider = new FieldSlider(content);
        slider->setRange(0, stepsOf(field));
        slider->setTracking(!(field.flags & Expensive));
        slider->setEnabled(!(field.flags & ReadOnly));
        slider->setToolTip(QString::fromLatin1(field.key));

        grid->addWidget(name, row, 0);
        grid->addWidget(value, row++, 1);
        grid->addWidget(slider, row++, 0, 1, 2);
        m_rows.push_back({slider, value});

        // Enquanto arrasta, o número acompanha (inclusive nos campos aplicados só ao soltar)
        connect(slider, &QSlider::sliderMoved, this, [this, i](int position) {
            m_rows[i].value->setText(format(fields()[i], valueAt(fields()[i], position)));
        });
        connect(slider, &QSlider::valueChanged, this, [this, i](int position) {
            if (m_updating)
                return;
            fields()[i].set(m_values, valueAt(fields()[i], position));
            updateRow(i);
            emit valuesChanged(m_values);
        });
    }
    grid->setRowStretch(row, 1);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(m, m / 2, m, 0);
    buttons->setSpacing(m_params.spacing);
    QPushButton *clear = makeButton("Limpar lousa", this);
    QPushButton *defaults = makeButton("Restaurar padrões", this);
    QPushButton *save = makeButton("Salvar", this);
    buttons->addWidget(clear);
    buttons->addWidget(defaults);
    buttons->addWidget(save);
    outer->addLayout(buttons);
    connect(clear, &QPushButton::clicked, this, &TuningPanel::clearRequested);
    connect(defaults, &QPushButton::clicked, this, &TuningPanel::restoreDefaults);
    connect(save, &QPushButton::clicked, this, &TuningPanel::saveRequested);

    m_status = new QLabel(this);
    m_status->setObjectName("TuningStatus");
    m_status->setWordWrap(true);
    m_status->setContentsMargins(m, m / 2, m, m);
    outer->addWidget(m_status);

    setValues(m_values);
}

void TuningPanel::setValues(const TunableParams &values)
{
    m_values = values;
    m_updating = true;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        m_rows[i].slider->setValue(positionOf(fields()[i], fields()[i].get(m_values)));
        updateRow(i);
    }
    m_updating = false;
}

void TuningPanel::setStatus(const QString &text)
{
    m_status->setText(text);
}

void TuningPanel::updateRow(std::size_t index)
{
    // Mostra o valor real (pode ter vindo do params.json fora do passo do slider)
    m_rows[index].value->setText(format(fields()[index], fields()[index].get(m_values)));
}

void TuningPanel::restoreDefaults()
{
    setValues(TunableParams());
    emit valuesChanged(m_values);
    setStatus("Padrões restaurados");
}

bool TuningPanel::load(const QString &path, TunableParams *values, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = parseError.error != QJsonParseError::NoError ? parseError.errorString() : "não é um objeto JSON";
        return false;
    }

    const QJsonObject root = document.object();
    for (const Field &field : fields()) {
        if (field.flags & ReadOnly)
            continue;
        const QJsonValue value = root.value(QLatin1String(field.group)).toObject().value(QLatin1String(field.key));
        if (value.isDouble())
            field.set(*values, std::clamp(value.toDouble(), field.min, field.max));
    }
    return true;
}

bool TuningPanel::save(const QString &path, const TunableParams &values, QString *error)
{
    QJsonObject root;
    for (const Field &field : fields()) {
        if (field.flags & ReadOnly)
            continue;
        const double v = field.get(values);
        const QJsonValue json = field.decimals == 0 ? QJsonValue(static_cast<qint64>(std::llround(v))) : QJsonValue(v);
        QJsonObject group = root.value(QLatin1String(field.group)).toObject();
        group.insert(QLatin1String(field.key), json);
        root.insert(QLatin1String(field.group), group);
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}
