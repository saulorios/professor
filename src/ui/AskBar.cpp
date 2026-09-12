#include "AskBar.h"

#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>

namespace {

QPushButton *makeButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

AskBar::AskBar(QWidget *parent)
    : QWidget(parent)
{
    // Necessário para o QSS pintar o fundo de uma subclasse de QWidget
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(m_params.height);

    m_field = new QLineEdit(this);
    m_field->setObjectName("AskField");
    m_field->setPlaceholderText("Pergunte ao professor (Ctrl+Enter envia)...");
    m_field->setClearButtonEnabled(true);
    connect(m_field, &QLineEdit::returnPressed, this, &AskBar::submit);

    // Ctrl+Enter envia de qualquer lugar da janela
    auto *send = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(send, &QShortcut::activated, this, &AskBar::submit);

    m_ask = makeButton("Perguntar", this);
    m_ask->setObjectName("AskButton");
    m_stop = makeButton("Parar", this);
    m_continue = makeButton("Continuar", this);
    m_log = makeButton("Log", this);
    m_log->setCheckable(true);
    m_status = new QLabel(this);
    m_status->setObjectName("AskStatus");
    m_status->setFixedWidth(m_params.statusWidth);
    m_status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    connect(m_ask, &QPushButton::clicked, this, &AskBar::submit);
    connect(m_stop, &QPushButton::clicked, this, &AskBar::stopRequested);
    connect(m_continue, &QPushButton::clicked, this, [this] {
        setStepPending(false);
        emit continueRequested();
    });
    connect(m_log, &QPushButton::toggled, this, &AskBar::logToggled);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(m_params.margin, 0, m_params.margin, 0);
    layout->setSpacing(m_params.spacing);
    layout->addWidget(m_field, 1);
    layout->addWidget(m_ask);
    layout->addWidget(m_stop);
    layout->addWidget(m_continue);
    layout->addWidget(m_log);
    layout->addWidget(m_status);

    setBusy(false);
    setStepPending(false);
}

void AskBar::submit()
{
    const QString question = m_field->text().trimmed();
    if (question.isEmpty() || !m_ask->isEnabled())
        return;
    m_field->clear();
    emit asked(question);
}

void AskBar::setBusy(bool busy)
{
    m_ask->setEnabled(!busy);
    m_stop->setEnabled(busy);
    if (busy)
        setStepPending(false);
}

void AskBar::setStepPending(bool pending)
{
    m_continue->setVisible(pending);
}

void AskBar::setStatus(const QString &text)
{
    m_status->setText(text);
    m_status->setToolTip(text);
}
