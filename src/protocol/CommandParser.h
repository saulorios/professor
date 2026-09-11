#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>

// Parser incremental de JSON Lines: recebe o texto em pedaços (que podem cortar
// linhas no meio), separa por linha e emite um sinal por comando válido.
// Linhas inválidas geram um aviso no log e são ignoradas.
class CommandParser : public QObject
{
    Q_OBJECT

public:
    explicit CommandParser(QObject *parent = nullptr);

    // Mais um pedaço do fluxo
    void appendData(const QByteArray &data);

    // Fim do fluxo: processa a última linha, mesmo sem '\n' no final
    void finish();

    // Descarta o que estiver pendente e recomeça a contagem de linhas
    void reset();

signals:
    void commandParsed(const QJsonObject &command);

private:
    void parseLine(QByteArray line);

    QByteArray m_buffer;
    int m_lineNumber = 0;
};
