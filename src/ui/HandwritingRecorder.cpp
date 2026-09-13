#include "HandwritingRecorder.h"
#include "HandwritingCapture.h"

#include "handwriting/data/GlyphSerializer.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <cmath>

using namespace handwriting;

namespace {

constexpr double kPi = 3.141592653589793;

QString num(double value, int decimals = 3)
{
    return QString::number(value, 'f', decimals);
}

QString point(const QPointF &p)
{
    return QString("(%1, %2)").arg(num(p.x()), num(p.y()));
}

} // namespace

HandwritingRecorder::HandwritingRecorder(QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_database(defaultDatabasePath())
{
    setObjectName("HandwritingRecorder");
    setWindowTitle("Gravador de escrita manual");
    resize(m_params.initialSize);

    // --- Topo: caractere, atalhos das letras do primeiro lote, próximo id ---
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("Caractere:", this));
    m_character = new QLineEdit(this);
    m_character->setMaxLength(2);   // um caractere (pares substitutos contam 2)
    m_character->setFixedWidth(48);
    m_character->setAlignment(Qt::AlignCenter);
    top->addWidget(m_character);
    for (const QString &c : m_params.quickCharacters) {
        auto *button = new QPushButton(c, this);
        button->setFixedWidth(32);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [this, c] { setCharacter(c); });
        top->addWidget(button);
    }
    top->addSpacing(12);
    m_next = new QLabel(this);
    top->addWidget(m_next);
    top->addStretch(1);

    // --- Centro: área de captura e, à direita, variantes e detalhes ---
    m_capture = new HandwritingCapture(this);
    auto *side = new QVBoxLayout;
    side->addWidget(new QLabel("Variantes salvas", this));
    m_variants = new QListWidget(this);
    side->addWidget(m_variants, 1);
    m_delete = new QPushButton("Excluir variante", this);
    m_delete->setToolTip("Apaga do disco a variante selecionada na lista");
    side->addWidget(m_delete);
    side->addWidget(new QLabel("Detalhes", this));
    m_details = new QPlainTextEdit(this);
    m_details->setReadOnly(true);
    m_details->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    m_details->setFont(mono);
    side->addWidget(m_details, 2);
    auto *sideWidget = new QWidget(this);
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(m_params.sideWidth);

    auto *middle = new QHBoxLayout;
    middle->addWidget(m_capture, 1);
    middle->addWidget(sideWidget);

    // --- Base: ações, visualização e estado ---
    auto *clear = new QPushButton("Limpar", this);
    auto *undo = new QPushButton("Desfazer", this);
    auto *save = new QPushButton("Salvar variante", this);
    auto *reload = new QPushButton("Recarregar banco", this);
    m_showTrajectory = new QCheckBox("Mostrar trajetória", this);
    m_showTrajectory->setChecked(true);
    m_showPoints = new QCheckBox("Mostrar pontos", this);
    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(clear);
    bottom->addWidget(undo);
    bottom->addWidget(save);
    bottom->addSpacing(16);
    bottom->addWidget(m_showTrajectory);
    bottom->addWidget(m_showPoints);
    bottom->addStretch(1);
    bottom->addWidget(reload);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(middle, 1);
    layout->addLayout(bottom);
    layout->addWidget(m_status);

    connect(m_character, &QLineEdit::textChanged, this, [this] {
        m_capture->setCharacter(currentCharacter());
        refreshVariants();
    });
    connect(clear, &QPushButton::clicked, m_capture, &HandwritingCapture::clear);
    connect(undo, &QPushButton::clicked, m_capture, &HandwritingCapture::undo);
    connect(save, &QPushButton::clicked, this, &HandwritingRecorder::saveVariant);
    connect(reload, &QPushButton::clicked, this, &HandwritingRecorder::reloadDatabase);
    connect(m_showTrajectory, &QCheckBox::toggled, m_capture, &HandwritingCapture::setShowTrajectory);
    connect(m_showPoints, &QCheckBox::toggled, m_capture, &HandwritingCapture::setShowPoints);
    connect(m_capture, &HandwritingCapture::strokesChanged, this, &HandwritingRecorder::refreshDetails);
    connect(m_capture, &HandwritingCapture::captureStarted, this, [this] {
        m_variants->setCurrentItem(nullptr);
        refreshDetails();
    });
    connect(m_variants, &QListWidget::itemClicked, this, &HandwritingRecorder::loadSelectedVariant);
    connect(m_variants, &QListWidget::itemSelectionChanged, this, &HandwritingRecorder::updateButtons);
    connect(m_delete, &QPushButton::clicked, this, &HandwritingRecorder::deleteSelectedVariant);

    auto *undoKey = new QShortcut(QKeySequence::Undo, this);
    connect(undoKey, &QShortcut::activated, m_capture, &HandwritingCapture::undo);
    auto *saveKey = new QShortcut(QKeySequence::Save, this);
    connect(saveKey, &QShortcut::activated, this, &HandwritingRecorder::saveVariant);
    auto *clearKey = new QShortcut(QKeySequence(Qt::Key_Delete), this);
    connect(clearKey, &QShortcut::activated, m_capture, &HandwritingCapture::clear);

    reloadDatabase();
    setCharacter(m_params.quickCharacters.value(0));
}

QString HandwritingRecorder::defaultDatabasePath()
{
    const QString env = qEnvironmentVariable("LOUSA_ESCRITA");
    if (!env.isEmpty())
        return env;
    return QDir(QCoreApplication::applicationDirPath()).filePath("handwriting");
}

void HandwritingRecorder::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Sem compressão de eventos o mouse entrega todas as amostras, não uma por quadro
    m_previousCompression = QCoreApplication::testAttribute(Qt::AA_CompressHighFrequencyEvents);
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
}

void HandwritingRecorder::hideEvent(QHideEvent *event)
{
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, m_previousCompression);
    QWidget::hideEvent(event);
}

QString HandwritingRecorder::currentCharacter() const
{
    const QString text = m_character->text();
    const QList<uint> codes = text.toUcs4();
    return codes.isEmpty() ? QString() : QString::fromUcs4(reinterpret_cast<const char32_t *>(codes.constData()), 1);
}

void HandwritingRecorder::setCharacter(const QString &character)
{
    m_character->setText(character);
    m_capture->clear();
    m_capture->setFocus();
}

void HandwritingRecorder::reloadDatabase()
{
    QString error;
    const bool exists = QDir(m_database.root()).exists();
    if (exists && !m_database.load(&error))
        m_status->setText(error);
    else if (!m_database.warnings().isEmpty())
        m_status->setText(QString("%1 aviso(s): %2").arg(m_database.warnings().size()).arg(m_database.warnings().first()));
    else
        m_status->setText(QString("Banco: %1 · %2 variante(s)").arg(m_database.root()).arg(m_database.variantCount()));
    refreshVariants();
}

void HandwritingRecorder::refreshVariants()
{
    const QString character = currentCharacter();
    m_variants->clear();
    m_next->setText(character.isEmpty() ? "Informe o caractere"
                                        : QString("Próxima variante: %1").arg(m_database.nextVariantId(character)));
    if (const Glyph *glyph = m_database.glyph(character)) {
        for (const GlyphVariant &v : glyph->variants) {
            auto *item = new QListWidgetItem(QString("%1   %2 stroke(s) · %3 ms")
                                                 .arg(v.variantId)
                                                 .arg(v.strokes.size())
                                                 .arg(qRound(v.metrics.durationMs)),
                                             m_variants);
            item->setData(Qt::UserRole, v.variantId);
        }
    }
    updateButtons();
    refreshDetails();
}

void HandwritingRecorder::loadSelectedVariant()
{
    const QListWidgetItem *item = m_variants->currentItem();
    if (!item)
        return;
    if (const GlyphVariant *v = m_database.variant(currentCharacter(), item->data(Qt::UserRole).toString()))
        m_capture->showVariant(*v);
}

void HandwritingRecorder::deleteSelectedVariant()
{
    const QListWidgetItem *item = m_variants->currentItem();
    if (!item || !item->isSelected())
        return;
    const QString character = currentCharacter();
    const QString id = item->data(Qt::UserRole).toString();
    const auto answer = QMessageBox::question(
        this, "Excluir variante",
        QString("Apagar a variante %1 do banco?\n\n%2\n\nIsto não pode ser desfeito.")
            .arg(id, m_database.variantPath(character, id)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!m_database.removeVariant(character, id, &error)) {
        m_status->setText(QString("Não foi possível excluir %1: %2").arg(id, error));
        return;
    }
    m_status->setText(QString("%1 excluída").arg(id));
    m_capture->clear();
    refreshVariants();
}

void HandwritingRecorder::updateButtons()
{
    const QListWidgetItem *item = m_variants->currentItem();
    m_delete->setEnabled(item && item->isSelected());
}

void HandwritingRecorder::refreshDetails()
{
    if (m_capture->isShowingVariant()) {
        const QListWidgetItem *item = m_variants->currentItem();
        const GlyphVariant *v =
            item ? m_database.variant(currentCharacter(), item->data(Qt::UserRole).toString()) : nullptr;
        m_details->setPlainText(v ? describe(*v) : QString());
        return;
    }
    const StrokeRecorder &recorder = m_capture->recorder();
    if (recorder.strokes().empty()) {
        m_details->setPlainText("Escreva a letra na área à esquerda.\n\n"
                                "Cada contato com a superfície é um stroke.\n"
                                "Ctrl+Z desfaz, Delete limpa, Ctrl+S salva.");
        return;
    }
    m_details->setPlainText(describe(recorder.build(currentCharacter(), "(não salva)")));
}

void HandwritingRecorder::saveVariant()
{
    const QString character = currentCharacter();
    if (character.isEmpty()) {
        m_status->setText("Informe o caractere antes de salvar.");
        return;
    }
    const StrokeRecorder &recorder = m_capture->recorder();
    if (m_capture->isShowingVariant() || recorder.strokes().empty()) {
        m_status->setText("Nada para salvar: escreva a letra primeiro.");
        return;
    }
    QString error;
    const GlyphVariant *saved = m_database.addVariant(recorder.build(character, QString()), &error);
    if (!saved) {
        m_status->setText(QString("Não foi possível salvar: %1").arg(error));
        return;
    }
    const QString id = saved->variantId;
    m_status->setText(QString("%1 salva em %2").arg(id, m_database.variantPath(character, id)));
    m_capture->clear();
    refreshVariants();
}

QString HandwritingRecorder::describe(const GlyphVariant &v) const
{
    const GlyphMetrics &g = v.metrics;
    QStringList lines;
    lines << QString("%1  \"%2\"  (%3)").arg(v.variantId, v.character, serializer::pointerName(v.capture.pointer));
    lines << "espaço do glifo: 1 = altura da guia, baseline y = 0, Y ↓";
    lines << QString("bounds   x %1  y %2  w %3  h %4")
                 .arg(num(g.bounds.x()), num(g.bounds.y()), num(g.bounds.width()), num(g.bounds.height()));
    lines << QString("centro   %1").arg(point(g.bounds.center()));
    lines << QString("ascent   %1   descent %2").arg(num(g.ascent()), num(g.descent()));
    lines << QString("início   %1   fim %2").arg(point(g.start), point(g.end));
    lines << QString("duração  %1 ms (giz encostado %2, levantado %3)")
                 .arg(num(g.durationMs, 1), num(g.inkMs, 1), num(g.penUpMs, 1));
    lines << QString("captura  %1 px/unidade, %2 × %3 px")
                 .arg(num(v.capture.unitPx, 1), num(v.capture.areaPx.width(), 0), num(v.capture.areaPx.height(), 0));

    double previousEnd = 0.0;
    for (std::size_t i = 0; i < v.strokes.size(); ++i) {
        const Stroke &s = v.strokes[i];
        const StrokeMetrics &m = s.metrics;
        lines << "";
        if (i > 0)
            lines << QString("  ↑ pen up  %1 ms").arg(num(s.startMs - previousEnd, 1));
        lines << QString("stroke %1  ↓ pen down  %2 → %3 ms").arg(s.id + 1).arg(num(s.startMs, 1), num(s.endMs(), 1));
        lines << QString("  pontos     %1 (crus %2)").arg(s.points.size()).arg(s.rawPoints.size());
        lines << QString("  início     %1").arg(point(m.start));
        lines << QString("  fim        %1").arg(point(m.end));
        lines << QString("  compr.     %1   duração %2 ms").arg(num(m.length), num(m.durationMs, 1));
        lines << QString("  direção    %1°   entrada %2°")
                     .arg(num(m.direction * 180.0 / kPi, 1), num(m.initialDirection * 180.0 / kPi, 1));
        lines << QString("  vel. média %1 u/s").arg(num(m.meanVelocity, 2));
        QStringList times;
        for (std::size_t k = 0; k < s.points.size() && int(k) < m_params.timestampsShown; ++k)
            times << num(s.points[k].timeMs, 1);
        if (int(s.points.size()) > m_params.timestampsShown)
            times << "…";
        lines << QString("  t (ms)     %1").arg(times.join(", "));
        previousEnd = s.endMs();
    }
    return lines.join('\n');
}
