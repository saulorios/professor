#include "GlyphDatabase.h"

#include "GlyphGeometry.h"
#include "GlyphSerializer.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace handwriting {

GlyphDatabase::GlyphDatabase(const QString &root, const HandwritingParams &params)
    : m_params(params)
    , m_root(root)
{
}

void GlyphDatabase::setRoot(const QString &root)
{
    m_root = root;
    m_glyphs.clear();
    m_warnings.clear();
}

QString GlyphDatabase::characterKey(const QString &character)
{
    QStringList parts;
    for (char32_t code : character.toUcs4())
        parts << QString("%1").arg(uint(code), 4, 16, QChar('0')).toUpper();
    return parts.join('_');
}

QString GlyphDatabase::variantLabel(const QString &character)
{
    if (character.size() == 1) {
        const char16_t c = character.front().unicode();
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            return character;
    }
    return "U" + characterKey(character);
}

QString GlyphDatabase::variantPath(const QString &character, const QString &variantId) const
{
    return QDir(m_root).filePath(characterKey(character) + "/" + variantId + ".json");
}

int GlyphDatabase::variantNumber(const QString &character, const QString &variantId) const
{
    const QString label = variantLabel(character);
    if (!variantId.startsWith(label))
        return -1;
    const QString digits = variantId.mid(label.size());
    bool ok = false;
    const int number = digits.toInt(&ok);
    return ok && !digits.isEmpty() && digits.front().isDigit() ? number : -1;
}

int GlyphDatabase::highestOnDisk(const QString &character) const
{
    if (m_root.isEmpty())
        return 0;
    const QDir dir(QDir(m_root).filePath(characterKey(character)));
    int highest = 0;
    for (const QString &name : dir.entryList({"*.json"}, QDir::Files))
        highest = std::max(highest, variantNumber(character, QFileInfo(name).completeBaseName()));
    return highest;
}

bool GlyphDatabase::load(QString *error)
{
    m_glyphs.clear();
    m_warnings.clear();
    const QDir root(m_root);
    if (m_root.isEmpty() || !root.exists()) {
        if (error)
            *error = QString("pasta do banco não encontrada: %1").arg(m_root);
        return false;
    }
    for (const QString &sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir dir(root.filePath(sub));
        for (const QString &name : dir.entryList({"*.json"}, QDir::Files, QDir::Name)) {
            const QString path = dir.filePath(name);
            GlyphVariant variant;
            QString reason;
            if (!serializer::loadFile(path, &variant, &reason, m_params)) {
                m_warnings << QString("%1: %2").arg(path, reason);
                continue;
            }
            if (characterKey(variant.character) != sub) {
                m_warnings << QString("%1: caractere \"%2\" fora da sua pasta").arg(path, variant.character);
                continue;
            }
            insert(std::move(variant));
        }
    }
    return true;
}

QStringList GlyphDatabase::characters() const
{
    QStringList list;
    for (const auto &entry : m_glyphs)
        list << entry.first;
    return list;
}

const Glyph *GlyphDatabase::glyph(const QString &character) const
{
    const auto it = m_glyphs.find(character);
    return it == m_glyphs.end() || it->second.variants.empty() ? nullptr : &it->second;
}

const GlyphVariant *GlyphDatabase::variant(const QString &character, const QString &variantId) const
{
    const Glyph *g = glyph(character);
    if (!g)
        return nullptr;
    for (const GlyphVariant &v : g->variants)
        if (v.variantId == variantId)
            return &v;
    return nullptr;
}

int GlyphDatabase::variantCount() const
{
    int count = 0;
    for (const auto &entry : m_glyphs)
        count += int(entry.second.variants.size());
    return count;
}

QString GlyphDatabase::nextVariantId(const QString &character) const
{
    int highest = highestOnDisk(character);
    if (const Glyph *g = glyph(character))
        for (const GlyphVariant &v : g->variants)
            highest = std::max(highest, variantNumber(character, v.variantId));
    return variantLabel(character)
           + QString("%1").arg(highest + 1, m_params.variantDigits, 10, QChar('0'));
}

const GlyphVariant *GlyphDatabase::addVariant(GlyphVariant variant, QString *error)
{
    const auto fail = [error](const QString &message) -> const GlyphVariant * {
        if (error)
            *error = message;
        return nullptr;
    };
    if (variant.character.isEmpty())
        return fail("informe o caractere");
    if (variant.strokes.empty())
        return fail("a variante não tem strokes");
    if (m_root.isEmpty())
        return fail("pasta do banco não definida");

    const QString dir = QDir(m_root).filePath(characterKey(variant.character));
    if (!QDir().mkpath(dir))
        return fail(QString("não foi possível criar %1").arg(dir));

    geometry::updateDerived(variant, m_params);
    // O id é recalculado a cada tentativa: se outro processo gravou o mesmo
    // número entre a consulta e a gravação, o arquivo dele fica intacto
    QString reason;
    for (int attempt = 0; attempt < 1000; ++attempt) {
        variant.variantId = nextVariantId(variant.character);
        const QString path = variantPath(variant.character, variant.variantId);
        if (serializer::saveFile(path, variant, false, &reason, m_params)) {
            const QString character = variant.character;
            const QString id = variant.variantId;
            insert(std::move(variant));
            return this->variant(character, id);
        }
        if (!QFileInfo::exists(path))
            break;   // erro de disco, não colisão
    }
    return fail(reason);
}

void GlyphDatabase::insert(GlyphVariant variant)
{
    Glyph &g = m_glyphs[variant.character];
    g.character = variant.character;
    const QString character = variant.character;
    // Id repetido (dois arquivos iguais em pastas copiadas): a primeira fica
    for (const GlyphVariant &v : g.variants) {
        if (v.variantId == variant.variantId) {
            m_warnings << QString("variante %1 repetida, ignorada").arg(variant.variantId);
            return;
        }
    }
    g.variants.push_back(std::move(variant));
    std::sort(g.variants.begin(), g.variants.end(), [this, &character](const GlyphVariant &a, const GlyphVariant &b) {
        return variantNumber(character, a.variantId) < variantNumber(character, b.variantId);
    });
}

} // namespace handwriting
