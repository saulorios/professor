#pragma once

#include "handwriting/HandwritingParams.h"
#include "handwriting/types/HandwritingTypes.h"

#include <QString>
#include <QStringList>

#include <map>

namespace handwriting {

// Banco de glifos manuscritos numa pasta:
//
//   <raiz>/0041/A01.json   ← "A", variante 01
//   <raiz>/0041/A02.json
//   <raiz>/0061/a01.json   ← "a" (pasta própria: sem colisão em sistemas de
//   <raiz>/00E1/U00E101.json    arquivos que não diferenciam maiúsculas)
//
// A pasta é o código Unicode do caractere; o id da variante é o próprio
// caractere quando ASCII alfanumérico, senão "U" + código, seguido do número.
// Cada variante nova recebe o próximo número livre e é gravada sem nunca
// sobrescrever um arquivo existente.
class GlyphDatabase
{
public:
    explicit GlyphDatabase(const QString &root = QString(), const HandwritingParams &params = HandwritingParams());

    void setRoot(const QString &root);
    const QString &root() const { return m_root; }

    // Lê todas as variantes da pasta. Arquivos inválidos são pulados e listados
    // em warnings(). Devolve false só se a pasta não puder ser lida.
    bool load(QString *error = nullptr);
    const QStringList &warnings() const { return m_warnings; }

    QStringList characters() const;
    const Glyph *glyph(const QString &character) const;
    const GlyphVariant *variant(const QString &character, const QString &variantId) const;
    int variantCount() const;

    // Próximo id livre, considerando a memória e a pasta (outra instância pode ter gravado)
    QString nextVariantId(const QString &character) const;

    // Grava como variante nova: o id é gerado aqui (o de `variant` é ignorado).
    // Devolve a variante como ficou no banco (válida até a próxima alteração
    // do banco), ou nullptr com `error`.
    const GlyphVariant *addVariant(GlyphVariant variant, QString *error = nullptr);

    // Apaga a variante do disco e do banco. As outras ficam intactas; se era a
    // de maior número, esse número volta a ser o próximo livre.
    bool removeVariant(const QString &character, const QString &variantId, QString *error = nullptr);

    static QString characterKey(const QString &character);   // "0041", "0065_0301"
    static QString variantLabel(const QString &character);   // "A", "a", "U00E1"
    QString variantPath(const QString &character, const QString &variantId) const;

private:
    int variantNumber(const QString &character, const QString &variantId) const;
    int highestOnDisk(const QString &character) const;
    void insert(GlyphVariant variant);

    HandwritingParams m_params;
    QString m_root;
    std::map<QString, Glyph> m_glyphs;   // por caractere
    QStringList m_warnings;
};

} // namespace handwriting
