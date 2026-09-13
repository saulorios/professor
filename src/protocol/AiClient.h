#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>

#include <vector>

class QNetworkReply;

// Parâmetros ajustáveis da conversa com a IA
struct AiClientParams {
    QString url = "http://127.0.0.1:8000/aula"; // proxy local (LOUSA_PROXY muda)
    int idleTimeoutMs = 60000;   // sem nenhum pedaço por esse tempo, desiste
    int maxMessages = 40;        // mensagens guardadas da conversa
    QString continueText = "continue"; // enviado pelo botão "Continuar"
};

// Fala com o proxy (que guarda a chave da API) e repassa o texto recebido em
// pedaços, do jeito que chega, para o CommandParser: o desenho começa assim que
// a primeira linha completa chega, sem esperar o fim da resposta.
// Tudo assíncrono pelo QNetworkAccessManager; nenhuma thread.
class AiClient : public QObject
{
    Q_OBJECT

public:
    explicit AiClient(QObject *parent = nullptr);

    // Pergunta nova (entra no histórico da conversa)
    void ask(const QString &question);
    // Resposta ao "fim_passo": pede a continuação da mesma aula
    void continueLesson();
    // Cancela a resposta em andamento (o que já chegou continua sendo desenhado)
    void stop();
    void clearHistory();

    bool isBusy() const { return m_reply != nullptr; }
    const AiClientParams &params() const { return m_params; }

signals:
    void started();
    void chunk(const QByteArray &data); // pedaço de texto, cru
    // `reused`: o proxy devolveu uma aula já respondida antes (nenhum token gasto)
    void finished(bool reused);
    void failed(const QString &message);

private:
    struct Message {
        QString role;   // "usuario" ou "professor"
        QString text;
    };

    void send();
    void readChunk();
    void replyFinished();

    AiClientParams m_params;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QTimer m_timeout;
    std::vector<Message> m_history;
    QString m_answer;      // resposta em construção (vai para o histórico no fim)
    QByteArray m_errorBody; // corpo das respostas de erro (não vira comando)
    bool m_aborted = false;
};
