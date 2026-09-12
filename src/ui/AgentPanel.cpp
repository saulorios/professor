#include "AgentPanel.h"

#include <QAbstractTextDocumentLayout>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStyle>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QElapsedTimer &sessionClock()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer;
}

QPushButton *makeButton(const QString &text, const QString &tip, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tip);
    return button;
}

} // namespace

// --- Cartão da timeline -----------------------------------------------------

TimelineCard::TimelineCard(const QString &text, QWidget *parent)
    : QFrame(parent)
    , question(text)
{
    setObjectName("TimelineCard");
    setCursor(Qt::PointingHandCursor);

    m_question = new QLabel(text, this);
    m_question->setObjectName("CardQuestion");
    m_question->setWordWrap(true);
    m_summary = new QLabel(this);
    m_summary->setObjectName("CardSummary");
    m_summary->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(2);
    layout->addWidget(m_question);
    layout->addWidget(m_summary);
    refresh();
}

void TimelineCard::setSummary(int commands, double seconds)
{
    m_commands = commands;
    m_seconds = seconds;
    refresh();
}

void TimelineCard::setState(State state)
{
    m_state = state;
    refresh();
}

void TimelineCard::setCurrent(bool current)
{
    if (property("atual").toBool() == current)
        return;
    setProperty("atual", current);
    style()->unpolish(this);
    style()->polish(this);
}

void TimelineCard::tick()
{
    if (m_state != State::Drawing)
        return;
    m_phase = (m_phase + 1) % 4;
    refresh();
}

void TimelineCard::refresh()
{
    const QString commands = QString("%1 comando%2").arg(m_commands).arg(m_commands == 1 ? "" : "s");
    switch (m_state) {
    case State::Drawing:
        m_summary->setText(QString("desenhando%1  ·  %2").arg(QString(m_phase, u'·'), commands));
        break;
    case State::Done:
        m_summary->setText(QString("%1 · %2 s").arg(commands).arg(m_seconds, 0, 'f', 0));
        break;
    case State::Stopped:
        m_summary->setText(QString("parado · %1").arg(commands));
        break;
    case State::Failed:
        m_summary->setText("erro ao falar com a IA");
        break;
    }
}

void TimelineCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QFrame::mousePressEvent(event);
}

// --- Painel -----------------------------------------------------------------

AgentPanel::AgentPanel(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(240);

    // Cabeçalho
    auto *header = new QWidget(this);
    header->setObjectName("AgentHeader");
    header->setFixedHeight(m_params.headerHeight);
    auto *title = new QLabel("Professor", header);
    title->setObjectName("AgentTitle");
    QPushButton *newLesson = makeButton("+", "Nova aula", header);
    QPushButton *history = makeButton("☰", "Histórico", header);
    QPushButton *close = makeButton("✕", "Fechar o painel (F8)", header);
    for (QPushButton *button : {newLesson, history, close})
        button->setObjectName("AgentIcon");
    auto *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(m_params.margin, 0, 4, 0);
    headerRow->setSpacing(2);
    headerRow->addWidget(title);
    headerRow->addStretch(1);
    headerRow->addWidget(newLesson);
    headerRow->addWidget(history);
    headerRow->addWidget(close);
    connect(newLesson, &QPushButton::clicked, this, &AgentPanel::newLessonRequested);
    connect(history, &QPushButton::clicked, this, &AgentPanel::historyRequested);
    connect(close, &QPushButton::clicked, this, &AgentPanel::closeRequested);

    // Timeline: do mais antigo (topo) ao mais recente
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("AgentTimeline");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_timelineContent = new QWidget(scroll);
    m_timelineContent->setObjectName("AgentTimelineContent");
    m_timeline = new QVBoxLayout(m_timelineContent);
    m_timeline->setContentsMargins(m_params.margin, m_params.margin, m_params.margin, m_params.margin);
    m_timeline->setSpacing(m_params.spacing);
    m_timeline->addStretch(1);
    scroll->setWidget(m_timelineContent);

    // Rodapé: campo de pergunta e botões
    m_input = new QPlainTextEdit(this);
    m_input->setObjectName("AgentInput");
    m_input->setPlaceholderText("Pergunte algo ao professor...");
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->setTabChangesFocus(true);
    connect(m_input, &QPlainTextEdit::textChanged, this, &AgentPanel::updateInputHeight);

    m_send = makeButton("➤", "Enviar (Ctrl+Enter)", this);
    m_send->setObjectName("AgentSend");
    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(m_params.spacing);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_send, 0, Qt::AlignBottom);

    m_stop = makeButton("Parar", "Cancelar a resposta em andamento", this);
    m_continue = makeButton("Continuar", "Pedir o próximo passo", this);
    m_log = makeButton("Log", "Mostrar os comandos recebidos", this);
    m_log->setCheckable(true);
    m_status = new QLabel(this);
    m_status->setObjectName("AgentStatus");
    m_status->setWordWrap(true);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(m_params.spacing);
    buttons->addWidget(m_stop);
    buttons->addWidget(m_continue);
    buttons->addWidget(m_log);
    buttons->addStretch(1);

    auto *disclaimer = new QLabel("A IA pode errar. Confira o que for importante.", this);
    disclaimer->setObjectName("AgentDisclaimer");
    disclaimer->setWordWrap(true);

    auto *footer = new QWidget(this);
    footer->setObjectName("AgentFooter");
    auto *footerColumn = new QVBoxLayout(footer);
    footerColumn->setContentsMargins(m_params.margin, m_params.spacing, m_params.margin, m_params.spacing);
    footerColumn->setSpacing(m_params.spacing);
    footerColumn->addLayout(inputRow);
    footerColumn->addLayout(buttons);
    footerColumn->addWidget(m_status);
    footerColumn->addWidget(disclaimer);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(header);
    column->addWidget(scroll, 1);
    column->addWidget(footer);

    connect(m_send, &QPushButton::clicked, this, &AgentPanel::submit);
    connect(m_stop, &QPushButton::clicked, this, &AgentPanel::stopRequested);
    connect(m_continue, &QPushButton::clicked, this, [this] {
        setStepPending(false);
        emit continueRequested();
    });
    connect(m_log, &QPushButton::toggled, this, &AgentPanel::logToggled);

    // Ctrl+Enter envia; Enter sozinho quebra linha no campo
    auto *send = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    send->setContext(Qt::WidgetWithChildrenShortcut);
    connect(send, &QShortcut::activated, this, &AgentPanel::submit);

    m_blink = new QTimer(this);
    m_blink->setInterval(m_params.blinkMs);
    connect(m_blink, &QTimer::timeout, this, [this] {
        if (m_current)
            m_current->tick();
    });

    updateInputHeight();
    setBusy(false);
    setStepPending(false);
}

void AgentPanel::updateInputHeight()
{
    // Cresce com o texto, até o limite de linhas
    const QFontMetrics metrics(m_input->font());
    const double documentHeight = m_input->document()->documentLayout()->documentSize().height();
    const int lines = std::clamp(int(std::ceil(documentHeight / std::max(1, metrics.lineSpacing()))), 1,
                                 m_params.inputLines);
    m_input->setFixedHeight(lines * metrics.lineSpacing() + 12);
}

void AgentPanel::submit()
{
    const QString question = m_input->toPlainText().trimmed();
    if (question.isEmpty() || !m_send->isEnabled())
        return;
    m_input->clear();
    emit asked(question);
}

void AgentPanel::setBusy(bool busy)
{
    m_send->setEnabled(!busy);
    m_stop->setEnabled(busy);
    if (busy)
        setStepPending(false);
}

void AgentPanel::setStepPending(bool pending)
{
    m_continue->setVisible(pending);
}

void AgentPanel::setStatus(const QString &text)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void AgentPanel::beginSegment(const QString &question, double topPixels)
{
    finishSegment(TimelineCard::State::Done); // o anterior fica como concluído

    auto *card = new TimelineCard(question, m_timelineContent);
    card->range = QRectF(0.0, topPixels, 1.0, 0.0);
    connect(card, &TimelineCard::clicked, this, [this, card] { emit goTo(card->range); });
    m_timeline->insertWidget(m_timeline->count() - 1, card); // antes do espaçador
    m_cards.push_back(card);
    m_current = card;
    m_startedAt = double(sessionClock().elapsed());
    m_blink->start();

    // Rola a timeline até o cartão novo
    if (auto *area = qobject_cast<QScrollArea *>(m_timelineContent->parentWidget()->parentWidget()))
        QTimer::singleShot(0, area, [area] { area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum()); });
}

void AgentPanel::extendSegment(const QRectF &canvasPixels)
{
    if (!m_current || canvasPixels.isNull())
        return;
    // A faixa é onde o desenho realmente ficou, não onde a caneta estava
    const double top = m_current->drew ? std::min(m_current->range.top(), canvasPixels.top())
                                       : canvasPixels.top();
    const double bottom = m_current->drew ? std::max(m_current->range.bottom(), canvasPixels.bottom())
                                          : canvasPixels.bottom();
    m_current->drew = true;
    m_current->range = QRectF(0.0, top, 1.0, bottom - top);
}

void AgentPanel::countCommand()
{
    if (!m_current)
        return;
    m_current->setSummary(++m_currentCommands, (double(sessionClock().elapsed()) - m_startedAt) / 1000.0);
}

void AgentPanel::finishSegment(TimelineCard::State state)
{
    if (!m_current)
        return;
    m_current->setSummary(m_currentCommands, (double(sessionClock().elapsed()) - m_startedAt) / 1000.0);
    m_current->setState(state);
    m_current = nullptr;
    m_currentCommands = 0;
    m_blink->stop();
}

void AgentPanel::clearTimeline()
{
    finishSegment(TimelineCard::State::Done);
    for (TimelineCard *card : m_cards)
        card->deleteLater();
    m_cards.clear();
}

void AgentPanel::setVisibleRange(double topPixels, double heightPixels)
{
    // Destaca o cartão do trecho que está à vista agora
    TimelineCard *visible = nullptr;
    double best = 0.0;
    for (TimelineCard *card : m_cards) {
        const double top = std::max(card->range.top(), topPixels);
        const double bottom = std::min(card->range.bottom(), topPixels + heightPixels);
        const double overlap = bottom - top;
        if (overlap > best) {
            best = overlap;
            visible = card;
        }
    }
    for (TimelineCard *card : m_cards)
        card->setCurrent(card == visible);
}
