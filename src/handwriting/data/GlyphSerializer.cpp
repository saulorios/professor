#include "GlyphSerializer.h"

#include "GlyphGeometry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <cmath>

namespace handwriting::serializer {

namespace {

double roundTo(double value, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

QJsonArray pointToJson(const WritingPoint &p, int posDecimals, const HandwritingParams &params)
{
    QJsonArray a{roundTo(p.pos.x(), posDecimals), roundTo(p.pos.y(), posDecimals),
                 roundTo(p.timeMs, params.timeDecimals)};
    const bool hasPressure = p.pressure >= 0.0f;
    const bool hasTilt = p.tilt >= 0.0f;
    if (hasPressure || hasTilt)
        a.append(hasPressure ? QJsonValue(roundTo(p.pressure, params.pressureDecimals)) : QJsonValue());
    if (hasTilt)
        a.append(roundTo(p.tilt, params.rawDecimals));
    return a;
}

bool pointFromJson(const QJsonValue &value, WritingPoint *p)
{
    const QJsonArray a = value.toArray();
    if (a.size() < 3 || !a[0].isDouble() || !a[1].isDouble() || !a[2].isDouble())
        return false;
    p->pos = QPointF(a[0].toDouble(), a[1].toDouble());
    p->timeMs = a[2].toDouble();
    p->pressure = a.size() > 3 && a[3].isDouble() ? float(a[3].toDouble()) : kUnknown;
    p->tilt = a.size() > 4 && a[4].isDouble() ? float(a[4].toDouble()) : kUnknown;
    return true;
}

QJsonArray pointArray(const QPointF &p, int decimals)
{
    return QJsonArray{roundTo(p.x(), decimals), roundTo(p.y(), decimals)};
}

QJsonArray rectArray(const QRectF &r, int decimals)
{
    return QJsonArray{roundTo(r.x(), decimals), roundTo(r.y(), decimals), roundTo(r.width(), decimals),
                      roundTo(r.height(), decimals)};
}

QPointF pointFrom(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    return a.size() >= 2 ? QPointF(a[0].toDouble(), a[1].toDouble()) : QPointF();
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

} // namespace

QString pointerName(PointerKind kind)
{
    switch (kind) {
    case PointerKind::Mouse: return "mouse";
    case PointerKind::Pen: return "pen";
    case PointerKind::Touch: return "touch";
    case PointerKind::Unknown: break;
    }
    return "unknown";
}

PointerKind pointerFromName(const QString &name)
{
    if (name == "mouse")
        return PointerKind::Mouse;
    if (name == "pen")
        return PointerKind::Pen;
    if (name == "touch")
        return PointerKind::Touch;
    return PointerKind::Unknown;
}

QJsonObject toJson(const GlyphVariant &variant, const HandwritingParams &params)
{
    const int d = params.glyphDecimals;
    QJsonArray strokes;
    for (const Stroke &stroke : variant.strokes) {
        QJsonArray points;
        for (const WritingPoint &p : stroke.points)
            points.append(pointToJson(p, d, params));
        QJsonArray raw;
        for (const WritingPoint &p : stroke.rawPoints)
            raw.append(pointToJson(p, params.rawDecimals, params));
        const StrokeMetrics &m = stroke.metrics;
        strokes.append(QJsonObject{
            {"id", stroke.id},
            {"startMs", roundTo(stroke.startMs, params.timeDecimals)},
            {"metrics", QJsonObject{{"start", pointArray(m.start, d)},
                                    {"end", pointArray(m.end, d)},
                                    {"bounds", rectArray(m.bounds, d)},
                                    {"length", roundTo(m.length, d)},
                                    {"durationMs", roundTo(m.durationMs, params.timeDecimals)},
                                    {"direction", roundTo(m.direction, d)},
                                    {"initialDirection", roundTo(m.initialDirection, d)}}},
            {"points", points},
            {"raw", raw},
        });
    }

    const GlyphMetrics &g = variant.metrics;
    const CaptureInfo &c = variant.capture;
    return QJsonObject{
        {"format", kFormat},
        {"schemaVersion", params.schemaVersion},
        {"character", variant.character},
        {"variantId", variant.variantId},
        {"pointFormat", QJsonArray{"x", "y", "t", "pressure", "tilt"}},
        {"capture", QJsonObject{{"pointer", pointerName(c.pointer)},
                                {"unitPx", roundTo(c.unitPx, params.rawDecimals)},
                                {"originPx", pointArray(c.originPx, params.rawDecimals)},
                                {"areaPx", QJsonArray{roundTo(c.areaPx.width(), params.rawDecimals),
                                                      roundTo(c.areaPx.height(), params.rawDecimals)}},
                                {"recordedAt", c.recordedAt.toUTC().toString(Qt::ISODateWithMs)}}},
        {"metrics", QJsonObject{{"bounds", rectArray(g.bounds, d)},
                                {"baseline", kBaseline},
                                {"start", pointArray(g.start, d)},
                                {"end", pointArray(g.end, d)},
                                {"durationMs", roundTo(g.durationMs, params.timeDecimals)},
                                {"inkMs", roundTo(g.inkMs, params.timeDecimals)},
                                {"penUpMs", roundTo(g.penUpMs, params.timeDecimals)}}},
        {"strokes", strokes},
    };
}

bool fromJson(const QJsonObject &json, GlyphVariant *variant, QString *error, const HandwritingParams &params)
{
    if (json.value("format").toString() != kFormat)
        return fail(error, "não é um arquivo de glifo manuscrito");
    const int version = json.value("schemaVersion").toInt(0);
    if (version < 1 || version > params.schemaVersion)
        return fail(error, QString("schemaVersion %1 não suportada (máx. %2)").arg(version).arg(params.schemaVersion));

    GlyphVariant v;
    v.character = json.value("character").toString();
    v.variantId = json.value("variantId").toString();
    if (v.character.isEmpty() || v.variantId.isEmpty())
        return fail(error, "\"character\" e \"variantId\" são obrigatórios");

    const QJsonObject capture = json.value("capture").toObject();
    v.capture.pointer = pointerFromName(capture.value("pointer").toString());
    v.capture.unitPx = capture.value("unitPx").toDouble(1.0);
    v.capture.originPx = pointFrom(capture.value("originPx"));
    const QJsonArray area = capture.value("areaPx").toArray();
    if (area.size() >= 2)
        v.capture.areaPx = QSizeF(area[0].toDouble(), area[1].toDouble());
    v.capture.recordedAt = QDateTime::fromString(capture.value("recordedAt").toString(), Qt::ISODateWithMs);

    const QJsonArray strokes = json.value("strokes").toArray();
    if (strokes.isEmpty())
        return fail(error, "a variante não tem strokes");
    for (const QJsonValue &value : strokes) {
        const QJsonObject s = value.toObject();
        Stroke stroke;
        stroke.id = s.value("id").toInt(int(v.strokes.size()));
        stroke.startMs = s.value("startMs").toDouble();
        for (const QJsonValue &p : s.value("points").toArray()) {
            WritingPoint point;
            if (!pointFromJson(p, &point))
                return fail(error, QString("ponto inválido no stroke %1").arg(stroke.id));
            stroke.points.push_back(point);
        }
        for (const QJsonValue &p : s.value("raw").toArray()) {
            WritingPoint point;
            if (!pointFromJson(p, &point))
                return fail(error, QString("ponto cru inválido no stroke %1").arg(stroke.id));
            stroke.rawPoints.push_back(point);
        }
        if (stroke.points.empty())
            return fail(error, QString("stroke %1 sem pontos").arg(stroke.id));
        v.strokes.push_back(std::move(stroke));
    }

    geometry::updateDerived(v, params);
    *variant = std::move(v);
    return true;
}

bool saveFile(const QString &path, const GlyphVariant &variant, bool overwrite, QString *error,
              const HandwritingParams &params)
{
    const QByteArray data = QJsonDocument(toJson(variant, params)).toJson(QJsonDocument::Indented);
    if (overwrite) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            return fail(error, file.errorString());
        return true;
    }
    // NewOnly: a abertura falha se o arquivo existir, sem janela para corrida
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(error, file.exists() ? QString("%1 já existe").arg(path) : file.errorString());
    if (file.write(data) != data.size()) {
        const QString message = file.errorString();
        file.remove();
        return fail(error, message);
    }
    return true;
}

bool loadFile(const QString &path, GlyphVariant *variant, QString *error, const HandwritingParams &params)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, file.errorString());
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError)
        return fail(error, parse.errorString());
    return fromJson(doc.object(), variant, error, params);
}

} // namespace handwriting::serializer
