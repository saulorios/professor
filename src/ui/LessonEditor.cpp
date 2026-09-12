#include "LessonEditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

// Modelos com os campos obrigatórios já preenchidos
struct Template {
    const char *type;
    const char *line;
};

const Template kTemplates[] = {
    {"fala", R"({"tipo":"fala","texto":"..."})"},
    {"pausa", R"({"tipo":"pausa","segundos":1})"},
    {"escrever", R"({"tipo":"escrever","id":"titulo","texto":"...","tamanho":6,"ancora":"topo_centro"})"},
    {"forma", R"({"tipo":"forma","id":"c1","forma":"circulo","raio":6,"ancora":"centro"})"},
    {"conectar", R"({"tipo":"conectar","de":"c1","ate":"c2","seta":false})"},
    {"destacar", R"({"tipo":"destacar","alvo":"titulo","modo":"sublinhar"})"},
    {"objeto_3d",
     R"({"tipo":"objeto_3d","id":"cubo","vista":"cavaleira","ancora":"centro","partes":[{"solido":"caixa","tamanho":[24,24,24]}]})"},
    {"rotular", R"({"tipo":"rotular","alvo":"cubo","vertice":"frente_topo_direita","texto":"A"})"},
    {"cotar", R"({"tipo":"cotar","alvo":"cubo","aresta":"largura","texto":"a"})"},
    {"apagar", R"({"tipo":"apagar","id":"c1"})"},
    {"limpar", R"({"tipo":"limpar"})"},
};

QPushButton *makeButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

LessonEditor::LessonEditor(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(m_params.height);

    m_text = new QPlainTextEdit(this);
    m_text->setObjectName("LessonText");
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setPlaceholderText("Um comando JSON por linha (ver docs/ia-protocol.md)");

    auto *apply = makeButton("Aplicar e reproduzir", this);
    apply->setObjectName("AskButton");
    auto *insert = makeButton("Inserir comando", this);
    auto *open = makeButton("Abrir...", this);
    auto *save = makeButton("Salvar...", this);
    m_status = new QLabel(this);
    m_status->setObjectName("LessonStatus");
    m_status->setWordWrap(true);

    auto *menu = new QMenu(insert);
    for (const Template &item : kTemplates) {
        const QString type = QString::fromLatin1(item.type);
        const QString line = QString::fromUtf8(item.line);
        menu->addAction(type, this, [this, line] { insertTemplate(line); });
    }
    insert->setMenu(menu);

    connect(apply, &QPushButton::clicked, this, &LessonEditor::applyRequested);
    connect(open, &QPushButton::clicked, this, &LessonEditor::openRequested);
    connect(save, &QPushButton::clicked, this, &LessonEditor::saveRequested);

    // Ctrl+Enter aplica quando o foco está no editor (a barra de perguntas tem
    // o mesmo atalho para o campo dela). Ctrl+S vem do menu File, que é global.
    auto *applyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    applyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(applyShortcut, &QShortcut::activated, this, &LessonEditor::applyRequested);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(m_params.spacing);
    buttons->addWidget(apply);
    buttons->addWidget(insert);
    buttons->addWidget(open);
    buttons->addWidget(save);
    buttons->addWidget(m_status, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(m_params.margin, m_params.margin, m_params.margin, m_params.margin);
    layout->setSpacing(m_params.spacing);
    layout->addWidget(m_text, 1);
    layout->addLayout(buttons);
}

QString LessonEditor::text() const
{
    return m_text->toPlainText();
}

void LessonEditor::setText(const QString &text)
{
    m_text->setPlainText(text);
    setErrors({});
}

void LessonEditor::setErrors(const QStringList &errors)
{
    m_status->setText(errors.isEmpty() ? QString() : errors.join("  ·  "));
    m_status->setProperty("erro", !errors.isEmpty());
    style()->unpolish(m_status);
    style()->polish(m_status);
}

void LessonEditor::insertTemplate(const QString &line)
{
    QTextCursor cursor = m_text->textCursor();
    cursor.movePosition(QTextCursor::EndOfLine);
    const bool empty = cursor.block().text().trimmed().isEmpty();
    cursor.insertText(empty ? line : "\n" + line);
    m_text->setTextCursor(cursor);
    m_text->setFocus();
}
