#include "AiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>

AiClient::AiClient(QObject *parent)
    : QObject(parent)
{
    const QByteArray url = qgetenv("LOUSA_PROXY");
    if (!url.isEmpty())
        m_params.url = QString::fromUtf8(url);

    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_reply)
            return;
        m_aborted = true;
        m_reply->abort();
        emit failed(QString("A IA não respondeu em %1 s.")
                        .arg(QString::number(m_params.idleTimeoutMs / 1000.0, 'g', 2)));
    });
}

void AiClient::ask(const QString &question)
{
    const QString text = question.trimmed();
    if (text.isEmpty())
        return;
    m_history.push_back({"usuario", text});
    send();
}

void AiClient::continueLesson()
{
    // O "continue" também entra no histórico: a IA precisa saber onde parou
    m_history.push_back({"usuario", m_params.continueText});
    send();
}

void AiClient::clearHistory()
{
    m_history.clear();
}

void AiClient::stop()
{
    if (!m_reply)
        return;
    m_aborted = true;
    m_reply->abort();
}

void AiClient::send()
{
    if (m_reply) // uma resposta por vez
        stop();

    // Só as últimas mensagens vão no pedido (a conversa não cresce sem limite)
    QJsonArray messages;
    const std::size_t first = m_history.size() > std::size_t(m_params.maxMessages)
                                  ? m_history.size() - std::size_t(m_params.maxMessages)
                                  : 0;
    for (std::size_t i = first; i < m_history.size(); ++i)
        messages.append(QJsonObject{{"papel", m_history[i].role}, {"texto", m_history[i].text}});

    QNetworkRequest request{QUrl(m_params.url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QJsonObject body{{"mensagens", messages}};

    m_answer.clear();
    m_errorBody.clear();
    m_aborted = false;
    m_reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &AiClient::readChunk);
    connect(m_reply, &QNetworkReply::finished, this, &AiClient::replyFinished);
    m_timeout.start(m_params.idleTimeoutMs);
    emit started();
}

void AiClient::readChunk()
{
    const QByteArray data = m_reply->readAll();
    if (data.isEmpty())
        return;
    m_timeout.start(m_params.idleTimeoutMs); // chegou algo: o relógio recomeça

    // Resposta de erro: o corpo é a explicação, não comandos para desenhar
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 0 && status != 200) {
        m_errorBody += data;
        return;
    }
    m_answer += QString::fromUtf8(data);
    emit chunk(data);
}

void AiClient::replyFinished()
{
    m_timeout.stop();
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    // O que chegou até aqui já faz parte da conversa, menos as linhas de erro
    // que o proxy acrescenta (não foram ditas pela IA)
    QStringList said;
    for (const QString &line : m_answer.split('\n')) {
        const QJsonObject object = QJsonDocument::fromJson(line.trimmed().toUtf8()).object();
        if (object.value("tipo").toString() != "erro")
            said << line;
    }
    const QString answer = said.join('\n');
    if (!answer.trimmed().isEmpty())
        m_history.push_back({"professor", answer});

    const QNetworkReply::NetworkError error = reply->error();
    if (m_aborted || error == QNetworkReply::NoError) {
        emit finished();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString message = reply->errorString();
    const QByteArray rest = m_errorBody + reply->readAll();
    if (!rest.isEmpty()) {
        // O proxy responde {"detail": "..."} nos erros; fora isso, o corpo cru
        const QJsonObject body = QJsonDocument::fromJson(rest).object();
        message = body.contains("detail") ? body.value("detail").toString()
                                          : QString::fromUtf8(rest).trimmed();
    }
    if (error == QNetworkReply::ConnectionRefusedError)
        message = QString("Proxy não encontrado em %1. Rode o servidor da pasta proxy/.").arg(m_params.url);
    emit failed(status > 0 ? QString("Erro %1: %2").arg(status).arg(message) : message);
}
