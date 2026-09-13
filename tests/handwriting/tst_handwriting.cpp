// Testes do banco de gestos de escrita manual (src/handwriting/)

#include "handwriting/data/GlyphDatabase.h"
#include "handwriting/data/GlyphGeometry.h"
#include "handwriting/data/GlyphSerializer.h"
#include "handwriting/engine/HandwritingEngine.h"
#include "handwriting/recorder/StrokeRecorder.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace handwriting;

namespace {

constexpr double kEps = 1e-6;

RawSample sample(double x, double y, double t, float pressure = kUnknown,
                 PointerKind pointer = PointerKind::Mouse)
{
    RawSample s;
    s.pos = QPointF(x, y);
    s.timeMs = t;
    s.pressure = pressure;
    s.pointer = pointer;
    return s;
}

CaptureGuide guide(double unitPx, double baselineY)
{
    CaptureGuide g;
    g.unitPx = unitPx;
    g.baselineY = baselineY;
    g.areaPx = QSizeF(800, 600);
    return g;
}

// "A" em px: duas diagonais e a barra, com pausas de giz levantado.
// `scale` e `offset` simulam outra resolução / posição na tela.
void writeA(StrokeRecorder &rec, double scale = 1.0, QPointF offset = QPointF(), double t0 = 1000.0,
            double wobble = 0.0)
{
    const auto p = [&](double x, double y) { return offset + QPointF(x, y) * scale; };
    // Guia: linha de base em y = 400, unidade = 200 px (escalados)
    rec.setGuide(guide(200.0 * scale, offset.y() + 400.0 * scale));

    rec.begin(sample(p(300, 400).x(), p(300, 400).y(), t0, 0.5f));
    rec.add(sample(p(350, 300 + wobble).x(), p(350, 300 + wobble).y(), t0 + 8, 0.6f));
    rec.end(sample(p(400, 200).x(), p(400, 200).y(), t0 + 16, 0.7f));

    rec.begin(sample(p(400, 200).x(), p(400, 200).y(), t0 + 100, 0.5f));
    rec.add(sample(p(450, 300).x(), p(450, 300).y(), t0 + 108, 0.6f));
    rec.end(sample(p(500, 400).x(), p(500, 400).y(), t0 + 116, 0.7f));

    rec.begin(sample(p(330, 330).x(), p(330, 330).y(), t0 + 250));
    rec.end(sample(p(470, 330).x(), p(470, 330).y(), t0 + 274));
}

} // namespace

class HandwritingTest : public QObject
{
    Q_OBJECT

private slots:
    void createStrokeAndAddPoints();
    void finishStrokeKeepsRawData();
    void dotStrokeIsKept();
    void undoAndClear();
    void timestampsAreRelative();
    void penUpDownPreserved();
    void boundingBoxAndMetrics();
    void strokeMetrics();
    void normalizationIndependentOfResolution();
    void saveAndLoadVariant();
    void unknownPressureStaysUnknown();
    void databaseAssignsSequentialIds();
    void databaseNeverOverwrites();
    void differentCapturesProduceDifferentVariants();
    void databaseLoadsFromDisk();
    void removeVariant();
    void charactersWithAccentsAndCase();
    void rejectsUnsupportedSchema();
    void trajectoryFromVariant();
    void placementDoesNotModifyVariant();
    void engineGeneratesText();
};

void HandwritingTest::createStrokeAndAddPoints()
{
    StrokeRecorder rec;
    rec.setGuide(guide(100, 300));
    QVERIFY(rec.isEmpty());
    rec.begin(sample(10, 300, 50));
    QVERIFY(rec.isStrokeOpen());
    rec.add(sample(20, 290, 58));
    rec.add(sample(20, 290, 58));   // repetido: nada é descartado
    QCOMPARE(rec.strokes().size(), std::size_t(1));
    QCOMPARE(rec.strokes().front().rawPoints.size(), std::size_t(3));
    QCOMPARE(rec.strokes().front().points.size(), std::size_t(3));
}

void HandwritingTest::finishStrokeKeepsRawData()
{
    StrokeRecorder rec;
    rec.setGuide(guide(100, 300));
    rec.begin(sample(10, 300, 50, 0.4f, PointerKind::Pen));
    rec.add(sample(20, 250, 60, 0.8f, PointerKind::Pen));
    rec.end(sample(20, 250, 70, 0.1f, PointerKind::Pen)); // mesma posição do último move: não entra
    QVERIFY(!rec.isStrokeOpen());
    const Stroke &s = rec.strokes().front();
    QCOMPARE(s.rawPoints.size(), std::size_t(2));
    QCOMPARE(s.rawPoints[1].pos, QPointF(20, 250));      // px originais
    QCOMPARE(s.rawPoints[1].pressure, 0.8f);
    QVERIFY(std::abs(s.points[1].pos.y() - (-0.5)) < kEps); // (250 − 300) / 100

    rec.begin(sample(30, 300, 100));
    rec.end(sample(40, 300, 110));   // posição nova: entra
    QCOMPARE(rec.strokes().back().rawPoints.size(), std::size_t(2));
}

void HandwritingTest::dotStrokeIsKept()
{
    StrokeRecorder rec;
    rec.setGuide(guide(100, 300));
    rec.begin(sample(50, 200, 0));
    rec.end(sample(50, 200, 30));    // o pingo do "i"
    const GlyphVariant v = rec.build("i", "i01");
    QCOMPARE(v.strokes.size(), std::size_t(1));
    QCOMPARE(v.strokes[0].points.size(), std::size_t(1));
    QCOMPARE(v.strokes[0].metrics.length, 0.0);
}

void HandwritingTest::undoAndClear()
{
    StrokeRecorder rec;
    writeA(rec);
    QCOMPARE(rec.strokes().size(), std::size_t(3));
    QVERIFY(rec.undo());
    QCOMPARE(rec.strokes().size(), std::size_t(2));
    QCOMPARE(rec.strokes().back().id, 1);
    rec.clear();
    QVERIFY(rec.isEmpty());
    QVERIFY(!rec.undo());
}

void HandwritingTest::timestampsAreRelative()
{
    StrokeRecorder rec;
    writeA(rec, 1.0, QPointF(), 123456.0);
    const GlyphVariant v = rec.build("A", "A01");
    const Stroke &s = v.strokes[0];
    QCOMPARE(s.startMs, 0.0);
    QCOMPARE(s.points[0].timeMs, 0.0);
    QCOMPARE(s.points[1].timeMs, 8.0);
    QCOMPARE(s.points[2].timeMs, 16.0);
    QCOMPARE(s.rawPoints[2].timeMs, 16.0);
    QCOMPARE(v.strokes[1].startMs, 100.0);
    QCOMPARE(v.strokes[1].points[0].timeMs, 0.0);

    // O relógio nunca anda para trás
    StrokeRecorder back;
    back.setGuide(guide(100, 300));
    back.begin(sample(0, 0, 100));
    back.add(sample(1, 0, 90));
    QVERIFY(back.strokes()[0].points[1].timeMs >= back.strokes()[0].points[0].timeMs);
}

void HandwritingTest::penUpDownPreserved()
{
    StrokeRecorder rec;
    writeA(rec);
    const GlyphVariant v = rec.build("A", "A01");
    // Pen down: 16 + 16 + 24 ms; pen up: (100 − 16) + (250 − 116) ms
    QCOMPARE(v.metrics.inkMs, 56.0);
    QCOMPARE(v.metrics.penUpMs, 218.0);
    QCOMPARE(v.metrics.durationMs, 274.0);

    QTemporaryDir dir;
    const QString path = dir.filePath("A01.json");
    QString error;
    QVERIFY2(serializer::saveFile(path, v, false, &error), qPrintable(error));
    GlyphVariant loaded;
    QVERIFY2(serializer::loadFile(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.metrics.penUpMs, 218.0);
    QCOMPARE(loaded.strokes[2].startMs, 250.0);
    QCOMPARE(loaded.strokes[1].endMs(), 116.0);
}

void HandwritingTest::boundingBoxAndMetrics()
{
    StrokeRecorder rec;
    writeA(rec);
    const GlyphVariant v = rec.build("A", "A01");
    // px x 300..500 → 0..1 (unidade 200, borda esquerda em 0); y 200..400 → −1..0
    const QRectF &b = v.metrics.bounds;
    QVERIFY(std::abs(b.left()) < kEps);
    QVERIFY(std::abs(b.width() - 1.0) < kEps);
    QVERIFY(std::abs(b.top() - (-1.0)) < kEps);
    QVERIFY(std::abs(b.bottom()) < kEps);
    QVERIFY(std::abs(v.metrics.ascent() - 1.0) < kEps);
    QVERIFY(std::abs(v.metrics.descent()) < kEps);
    QVERIFY(std::abs(b.center().x() - 0.5) < kEps);
    QCOMPARE(v.metrics.start, v.strokes.front().points.front().pos);
    QCOMPARE(v.metrics.end, v.strokes.back().points.back().pos);

    // Invariante da captura: points = (raw − originPx) / unitPx
    for (const Stroke &s : v.strokes)
        for (std::size_t i = 0; i < s.points.size(); ++i) {
            const QPointF expected = (s.rawPoints[i].pos - v.capture.originPx) / v.capture.unitPx;
            QVERIFY(std::abs(expected.x() - s.points[i].pos.x()) < kEps);
            QVERIFY(std::abs(expected.y() - s.points[i].pos.y()) < kEps);
        }
}

void HandwritingTest::strokeMetrics()
{
    StrokeRecorder rec;
    writeA(rec);
    const GlyphVariant v = rec.build("A", "A01");
    const StrokeMetrics &bar = v.strokes[2].metrics;
    QVERIFY(std::abs(bar.length - 0.7) < kEps);      // 140 px / 200
    QCOMPARE(bar.durationMs, 24.0);
    QVERIFY(std::abs(bar.direction) < kEps);         // para a direita
    QVERIFY(std::abs(bar.meanVelocity - 0.7 / 0.024) < 1e-3);

    const StrokeMetrics &left = v.strokes[0].metrics;
    QVERIFY(std::abs(left.direction - std::atan2(-1.0, 0.5)) < kEps); // sobe para a direita
    QVERIFY(v.strokes[0].points[1].velocity > 0.0);
}

void HandwritingTest::normalizationIndependentOfResolution()
{
    StrokeRecorder small;
    writeA(small, 1.0);
    StrokeRecorder large;
    writeA(large, 2.4, QPointF(137, 55));   // outra resolução e posição na tela
    const GlyphVariant a = small.build("A", "A01");
    const GlyphVariant b = large.build("A", "A02");
    QCOMPARE(a.strokes.size(), b.strokes.size());
    for (std::size_t s = 0; s < a.strokes.size(); ++s)
        for (std::size_t i = 0; i < a.strokes[s].points.size(); ++i) {
            QVERIFY(std::abs(a.strokes[s].points[i].pos.x() - b.strokes[s].points[i].pos.x()) < kEps);
            QVERIFY(std::abs(a.strokes[s].points[i].pos.y() - b.strokes[s].points[i].pos.y()) < kEps);
        }
    QVERIFY(a.strokes[0].rawPoints[1].pos != b.strokes[0].rawPoints[1].pos);   // crus preservados
}

void HandwritingTest::saveAndLoadVariant()
{
    StrokeRecorder rec;
    writeA(rec);
    const GlyphVariant v = rec.build("A", "A01");
    QTemporaryDir dir;
    const QString path = dir.filePath("A01.json");
    QString error;
    QVERIFY2(serializer::saveFile(path, v, false, &error), qPrintable(error));

    GlyphVariant loaded;
    QVERIFY2(serializer::loadFile(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.character, QString("A"));
    QCOMPARE(loaded.variantId, QString("A01"));
    QCOMPARE(loaded.capture.pointer, PointerKind::Mouse);
    QCOMPARE(loaded.strokes.size(), v.strokes.size());
    for (std::size_t s = 0; s < v.strokes.size(); ++s) {
        QCOMPARE(loaded.strokes[s].id, int(s));   // ordem dos strokes
        QCOMPARE(loaded.strokes[s].points.size(), v.strokes[s].points.size());
        QCOMPARE(loaded.strokes[s].rawPoints.size(), v.strokes[s].rawPoints.size());
        for (std::size_t i = 0; i < v.strokes[s].points.size(); ++i) {
            QCOMPARE(loaded.strokes[s].points[i].timeMs, v.strokes[s].points[i].timeMs);
            QCOMPARE(loaded.strokes[s].rawPoints[i].pos, v.strokes[s].rawPoints[i].pos);
            QVERIFY(std::abs(loaded.strokes[s].points[i].pos.x() - v.strokes[s].points[i].pos.x()) < 1e-4);
            QVERIFY(std::abs(loaded.strokes[s].points[i].pressure - v.strokes[s].points[i].pressure) < 1e-3f);
        }
    }
    QVERIFY(std::abs(loaded.metrics.bounds.width() - v.metrics.bounds.width()) < 1e-4);
    QVERIFY(loaded.capture.recordedAt.isValid());
}

void HandwritingTest::unknownPressureStaysUnknown()
{
    StrokeRecorder rec;
    writeA(rec);   // a barra do A foi escrita sem pressão
    const GlyphVariant v = rec.build("A", "A01");
    const QJsonObject json = serializer::toJson(v);
    const QJsonArray firstBarPoint =
        json["strokes"].toArray()[2].toObject()["points"].toArray()[0].toArray();
    QCOMPARE(firstBarPoint.size(), 3);   // [x, y, t]

    GlyphVariant loaded;
    QString error;
    QVERIFY2(serializer::fromJson(json, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.strokes[2].points[0].pressure, kUnknown);
    QCOMPARE(loaded.strokes[0].points[0].tilt, kUnknown);
    QVERIFY(loaded.strokes[0].points[0].pressure >= 0.0f);
}

void HandwritingTest::databaseAssignsSequentialIds()
{
    QTemporaryDir dir;
    GlyphDatabase db(dir.path());
    QCOMPARE(db.nextVariantId("A"), QString("A01"));
    StrokeRecorder rec;
    for (int i = 0; i < 3; ++i) {
        rec.clear();
        writeA(rec, 1.0, QPointF(), 0.0, i * 5.0);
        QString error;
        const GlyphVariant *saved = db.addVariant(rec.build("A", "ignorado"), &error);
        QVERIFY2(saved, qPrintable(error));
        QCOMPARE(saved->variantId, QString("A0%1").arg(i + 1));
    }
    QVERIFY(QFile::exists(dir.filePath("0041/A01.json")));
    QVERIFY(QFile::exists(dir.filePath("0041/A03.json")));
    QCOMPARE(db.glyph("A")->variants.size(), std::size_t(3));
    QCOMPARE(db.nextVariantId("A"), QString("A04"));
}

void HandwritingTest::databaseNeverOverwrites()
{
    QTemporaryDir dir;
    StrokeRecorder rec;
    writeA(rec);
    const GlyphVariant v = rec.build("A", "");

    GlyphDatabase first(dir.path());
    QVERIFY(first.addVariant(v));
    const QString path = dir.filePath("0041/A01.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray original = file.readAll();
    file.close();

    // Outra instância, que não carregou a pasta, também não pode pisar em A01
    GlyphDatabase second(dir.path());
    const GlyphVariant *saved = second.addVariant(v);
    QVERIFY(saved);
    QCOMPARE(saved->variantId, QString("A02"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    file.close();

    // E o serializer recusa gravar por cima sem pedido explícito
    QString error;
    QVERIFY(!serializer::saveFile(path, v, false, &error));
    QVERIFY(!error.isEmpty());
}

void HandwritingTest::differentCapturesProduceDifferentVariants()
{
    QTemporaryDir dir;
    GlyphDatabase db(dir.path());
    StrokeRecorder rec;
    writeA(rec, 1.0, QPointF(), 0.0, 0.0);
    const QString id1 = db.addVariant(rec.build("A", ""))->variantId;
    rec.clear();
    writeA(rec, 1.0, QPointF(), 0.0, 12.0);   // mesma letra, outro gesto
    const QString id2 = db.addVariant(rec.build("A", ""))->variantId;

    QVERIFY(id1 != id2);
    const GlyphVariant *a1 = db.variant("A", id1);
    const GlyphVariant *a2 = db.variant("A", id2);
    QVERIFY(a1 && a2);
    QCOMPARE(a1->character, a2->character);
    QVERIFY(a1->strokes[0].points[1].pos != a2->strokes[0].points[1].pos);
}

void HandwritingTest::databaseLoadsFromDisk()
{
    QTemporaryDir dir;
    {
        GlyphDatabase db(dir.path());
        StrokeRecorder rec;
        writeA(rec);
        QVERIFY(db.addVariant(rec.build("A", "")));
        QVERIFY(db.addVariant(rec.build("A", "")));
        QVERIFY(db.addVariant(rec.build("O", "")));
    }
    QFile junk(dir.filePath("0041/lixo.json"));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("{ não é json");
    junk.close();

    GlyphDatabase db(dir.path());
    QVERIFY(db.load());
    QCOMPARE(db.characters(), QStringList({"A", "O"}));
    QCOMPARE(db.variantCount(), 3);
    QCOMPARE(db.glyph("A")->variants[0].variantId, QString("A01"));
    QCOMPARE(db.glyph("A")->variants[1].variantId, QString("A02"));
    QCOMPARE(db.warnings().size(), 1);   // o arquivo inválido é pulado com aviso
    QCOMPARE(db.glyph("A")->variants[1].strokes.size(), std::size_t(3));
}

void HandwritingTest::removeVariant()
{
    QTemporaryDir dir;
    GlyphDatabase db(dir.path());
    StrokeRecorder rec;
    writeA(rec);
    for (int i = 0; i < 3; ++i)
        QVERIFY(db.addVariant(rec.build("A", "")));
    QVERIFY(db.addVariant(rec.build("O", "")));

    // Do meio: some do disco e da memória, as outras ficam, o número não volta
    QString error;
    QVERIFY2(db.removeVariant("A", "A02", &error), qPrintable(error));
    QVERIFY(!QFile::exists(dir.filePath("0041/A02.json")));
    QVERIFY(QFile::exists(dir.filePath("0041/A01.json")));
    QVERIFY(QFile::exists(dir.filePath("0041/A03.json")));
    QVERIFY(!db.variant("A", "A02"));
    QCOMPARE(db.glyph("A")->variants.size(), std::size_t(2));
    QCOMPARE(db.nextVariantId("A"), QString("A04"));

    // A de maior número: o número fica livre de novo
    QVERIFY(db.removeVariant("A", "A03"));
    QCOMPARE(db.nextVariantId("A"), QString("A02"));

    // Inexistentes: erro, nada muda
    QVERIFY(!db.removeVariant("A", "A09", &error));
    QVERIFY(error.contains("A09"));
    QVERIFY(!db.removeVariant("Z", "Z01"));
    QCOMPARE(db.variantCount(), 2);

    // Última variante do caractere: o glifo e a pasta vazia somem
    QVERIFY(db.removeVariant("O", "O01"));
    QVERIFY(!db.glyph("O"));
    QVERIFY(!QDir(dir.filePath("004F")).exists());

    GlyphDatabase reloaded(dir.path());
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.characters(), QStringList({"A"}));
    QCOMPARE(reloaded.variantCount(), 1);
}

void HandwritingTest::charactersWithAccentsAndCase()
{
    QCOMPARE(GlyphDatabase::variantLabel("A"), QString("A"));
    QCOMPARE(GlyphDatabase::variantLabel("a"), QString("a"));
    QCOMPARE(GlyphDatabase::variantLabel("7"), QString("7"));
    QCOMPARE(GlyphDatabase::variantLabel(QString::fromUtf8("á")), QString("U00E1"));
    QCOMPARE(GlyphDatabase::variantLabel(QString::fromUtf8("∫")), QString("U222B"));
    QCOMPARE(GlyphDatabase::characterKey("A"), QString("0041"));
    QCOMPARE(GlyphDatabase::characterKey("a"), QString("0061"));

    QTemporaryDir dir;
    GlyphDatabase db(dir.path());
    StrokeRecorder rec;
    writeA(rec);
    QVERIFY(db.addVariant(rec.build("A", "")));
    QCOMPARE(db.addVariant(rec.build("a", ""))->variantId, QString("a01"));
    QCOMPARE(db.addVariant(rec.build(QString::fromUtf8("ç"), ""))->variantId, QString("U00E701"));
    QCOMPARE(db.addVariant(rec.build(QString::fromUtf8("ç"), ""))->variantId, QString("U00E702"));

    GlyphDatabase reloaded(dir.path());
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.variantCount(), 4);
    QVERIFY(reloaded.variant(QString::fromUtf8("ç"), "U00E702"));
    QVERIFY(reloaded.glyph("a"));
}

void HandwritingTest::rejectsUnsupportedSchema()
{
    StrokeRecorder rec;
    writeA(rec);
    QJsonObject json = serializer::toJson(rec.build("A", "A01"));
    json["schemaVersion"] = 99;
    GlyphVariant out;
    QString error;
    QVERIFY(!serializer::fromJson(json, &out, &error));
    QVERIFY(error.contains("99"));
}

void HandwritingTest::trajectoryFromVariant()
{
    StrokeRecorder rec;
    writeA(rec);
    const GlyphVariant v = rec.build("A", "A01");

    WritingTrajectory t;
    GlyphPlacement placement;
    placement.origin = QPointF(10, 50);
    placement.size = 4.0;
    HandwritingEngine::appendGlyph(t, v, placement, 0.0);

    // Down, Up, Down, Up, Down
    QCOMPARE(t.segments.size(), std::size_t(5));
    for (std::size_t i = 0; i < t.segments.size(); ++i)
        QCOMPARE(t.segments[i].pen, i % 2 == 0 ? PenState::Down : PenState::Up);
    QCOMPARE(t.segments[1].startMs, 16.0);
    QCOMPARE(t.segments[1].endMs, 100.0);
    QCOMPARE(t.segments[1].from, t.segments[0].to);
    QCOMPARE(t.segments[1].to, t.segments[2].from);
    QCOMPARE(t.segments[4].stroke, 2);
    QCOMPARE(t.durationMs, 274.0);

    // Tempo sempre crescente, posição na lousa, velocidade e direção presentes
    for (std::size_t i = 1; i < t.points.size(); ++i)
        QVERIFY(t.points[i].timeMs >= t.points[i - 1].timeMs);
    QVERIFY(std::abs(t.points.front().pos.x() - 10.0) < kEps);   // borda esquerda na origem
    QVERIFY(std::abs(t.points.front().pos.y() - 50.0) < kEps);   // na linha de base
    QVERIFY(std::abs(t.bounds.height() - 4.0) < kEps);
    const TrajectoryPoint &barMid = t.points[t.segments[4].first];
    QVERIFY(std::abs(barMid.direction) < kEps);
    QVERIFY(barMid.velocity > 0.0);

    QCOMPARE(t.glyphs.size(), std::size_t(1));
    QCOMPARE(t.glyphs[0].variantId, QString("A01"));
    QCOMPARE(t.glyphs[0].segmentCount, std::size_t(5));
}

void HandwritingTest::placementDoesNotModifyVariant()
{
    StrokeRecorder rec;
    writeA(rec);
    const GlyphVariant v = rec.build("A", "A01");
    const QJsonObject before = serializer::toJson(v);

    WritingTrajectory t;
    GlyphPlacement placement;
    placement.size = 4.0;
    placement.scale = 0.98;
    placement.rotationDegrees = -1.2;
    placement.baselineOffset = 2.0;
    placement.speed = 1.03;
    HandwritingEngine::appendGlyph(t, v, placement, 0.0);

    QCOMPARE(serializer::toJson(v), before);   // BANCO = referência
    QVERIFY(std::abs(t.durationMs - 274.0 / 1.03) < 1e-6);   // ENGINE = variação
    QVERIFY(std::abs(t.points.front().pos.y() - 2.0) < 1e-6);
}

void HandwritingTest::engineGeneratesText()
{
    QTemporaryDir dir;
    GlyphDatabase db(dir.path());
    StrokeRecorder rec;
    writeA(rec, 1.0, QPointF(), 0.0, 0.0);
    db.addVariant(rec.build("A", ""));
    rec.clear();
    writeA(rec, 1.0, QPointF(), 0.0, 20.0);
    db.addVariant(rec.build("A", ""));

    HandwritingEngine engine(db);
    HandwritingOptions options;
    options.origin = QPointF(4, 20);
    options.size = 4.0;
    const WritingTrajectory t = engine.generate("AA A?", options);

    QCOMPARE(t.glyphs.size(), std::size_t(3));
    QCOMPARE(t.missing, QString("?"));
    QVERIFY(t.glyphs[1].bounds.left() > t.glyphs[0].bounds.right());
    QVERIFY(t.glyphs[2].bounds.left() > t.glyphs[1].bounds.right());
    QVERIFY(t.glyphs[1].startMs > t.glyphs[0].endMs);   // giz levantado entre letras
    // Entre glifos o trecho Up não pertence a nenhum glifo
    const TrajectorySegment &between = t.segments[t.glyphs[1].firstSegment - 1];
    QCOMPARE(between.pen, PenState::Up);
    QCOMPARE(between.glyph, -1);

    // Mesma seed, mesma escolha de variantes
    const WritingTrajectory again = engine.generate("AA A?", options);
    for (std::size_t i = 0; i < t.glyphs.size(); ++i)
        QCOMPARE(again.glyphs[i].variantId, t.glyphs[i].variantId);
}

QTEST_GUILESS_MAIN(HandwritingTest)
#include "tst_handwriting.moc"
