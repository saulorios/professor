#pragma once

#include "handwriting/HandwritingParams.h"
#include "handwriting/types/HandwritingTypes.h"

#include <QJsonObject>
#include <QString>

// Uma variante ↔ um arquivo JSON (formato em docs/handwriting-format.md).
// Pontos são gravados como arrays compactos [x, y, t, pressão, inclinação],
// com os valores ausentes no fim omitidos e os do meio como null. As medidas
// vão no arquivo só para inspeção: ao carregar, são recalculadas dos pontos.
namespace handwriting::serializer {

constexpr const char *kFormat = "lousa-handwriting-glyph";

QJsonObject toJson(const GlyphVariant &variant, const HandwritingParams &params = HandwritingParams());
bool fromJson(const QJsonObject &json, GlyphVariant *variant, QString *error,
              const HandwritingParams &params = HandwritingParams());

// `overwrite` false: falha se o arquivo já existe (nunca apaga uma variante)
bool saveFile(const QString &path, const GlyphVariant &variant, bool overwrite, QString *error,
              const HandwritingParams &params = HandwritingParams());
bool loadFile(const QString &path, GlyphVariant *variant, QString *error,
              const HandwritingParams &params = HandwritingParams());

QString pointerName(PointerKind kind);
PointerKind pointerFromName(const QString &name);

} // namespace handwriting::serializer
