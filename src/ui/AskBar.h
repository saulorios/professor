#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

// Parâmetros ajustáveis da barra de perguntas
struct AskBarParams {
    int height = 44;
    int margin = 10;
    int spacing = 6;
    int statusWidth = 260;   // largura reservada ao recado de erro/estado
};

// Barra onde o aluno pergunta ao professor virtual. Enter ou Ctrl+Enter envia.
// "Parar" cancela a resposta em andamento; "Continuar" só aparece quando a IA
// termina um passo (`fim_passo`); "Log" abre e fecha o painel de comandos.
class AskBar : public QWidget
{
    Q_OBJECT

public:
    explicit AskBar(QWidget *parent = nullptr);

public slots:
    void setBusy(bool busy);         // resposta em andamento
    void setStepPending(bool pending); // mostra o botão "Continuar"
    void setStatus(const QString &text);

signals:
    void asked(const QString &question);
    void stopRequested();
    void continueRequested();
    void logToggled(bool visible);

private:
    void submit();

    AskBarParams m_params;
    QLineEdit *m_field = nullptr;
    QPushButton *m_ask = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_continue = nullptr;
    QPushButton *m_log = nullptr;
    QLabel *m_status = nullptr;
};
