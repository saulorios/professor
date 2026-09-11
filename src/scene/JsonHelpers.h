#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QPointF>

// Leitura de campos dos comandos JSON
namespace json {

inline bool readNumber(const QJsonObject &object, const char *key, double *out)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble())
        return false;
    *out = value.toDouble();
    return true;
}

// Ponto no formato [x, y]
inline bool readPoint(const QJsonValue &value, QPointF *out)
{
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 2 || !array[0].isDouble() || !array[1].isDouble())
        return false;
    *out = QPointF(array[0].toDouble(), array[1].toDouble());
    return true;
}

} // namespace json
