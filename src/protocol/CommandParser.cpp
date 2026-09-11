#include "CommandParser.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>

CommandParser::CommandParser(QObject *parent)
    : QObject(parent)
{
}

void CommandParser::appendData(const QByteArray &data)
{
    m_buffer.append(data);
    qsizetype newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        parseLine(line);
    }
}

void CommandParser::finish()
{
    if (!m_buffer.isEmpty()) {
        const QByteArray line = m_buffer;
        m_buffer.clear();
        parseLine(line);
    }
}

void CommandParser::reset()
{
    m_buffer.clear();
    m_lineNumber = 0;
}

void CommandParser::parseLine(QByteArray line)
{
    ++m_lineNumber;
    if (m_lineNumber == 1 && line.startsWith("\xEF\xBB\xBF"))
        line.remove(0, 3); // BOM do UTF-8
    line = line.trimmed(); // também remove o '\r' de arquivos do Windows
    if (line.isEmpty())
        return;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning().noquote() << QString("Linha %1 ignorada: JSON inválido (%2, posição %3)")
                                    .arg(m_lineNumber).arg(error.errorString()).arg(error.offset);
        return;
    }
    if (!document.isObject()) {
        qWarning().noquote() << QString("Linha %1 ignorada: não é um objeto JSON").arg(m_lineNumber);
        return;
    }
    const QJsonObject command = document.object();
    if (!command.value("tipo").isString()) {
        qWarning().noquote() << QString("Linha %1 ignorada: falta o campo \"tipo\"").arg(m_lineNumber);
        return;
    }
    emit commandParsed(command);
}
