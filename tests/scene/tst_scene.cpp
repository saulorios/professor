// Testes da cena: expressões e contas do "grafico", e a escrita com as letras
// gravadas no banco de escrita manual

#include "handwriting/data/GlyphDatabase.h"
#include "handwriting/recorder/StrokeRecorder.h"
#include "scene/ChartGeometry.h"
#include "scene/Expression.h"
#include "scene/HersheyFont.h"
#include "scene/TextLayout.h"

#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

class SceneTest : public QObject
{
    Q_OBJECT

private slots:
    void expressionValues();
    void expressionImplicitAndUnicode();
    void expressionErrors();
    void ticksAndFormat();
    void autoRangeIncludesZero();
    void curveBreaksAtAsymptotes();
    void recordedLettersReplaceFont();
};

namespace {

double eval(const QString &text, double x)
{
    Expression e;
    QString error;
    if (!e.parse(text, &error))
        qWarning() << text << error;
    return e.evaluate(x);
}

} // namespace

void SceneTest::expressionValues()
{
    QCOMPARE(eval("x^2 - 2*x - 3", 1.0), -4.0);
    QCOMPARE(eval("-x^2", 2.0), -4.0);          // potência antes do menos
    QCOMPARE(eval("2^3^2", 0.0), 512.0);        // associativa à direita
    QCOMPARE(eval("x^-1", 4.0), 0.25);
    QVERIFY(std::abs(eval("sin(pi/2)", 0.0) - 1.0) < 1e-12);
    QVERIFY(std::abs(eval("ln(e)", 0.0) - 1.0) < 1e-12);
    QCOMPARE(eval("log(1000)", 0.0), 3.0);
    QCOMPARE(eval("abs(x - 5)", 2.0), 3.0);
    QVERIFY(std::isnan(eval("sqrt(x)", -1.0)));
}

void SceneTest::expressionImplicitAndUnicode()
{
    QCOMPARE(eval("2x - 3", 2.0), 1.0);
    QCOMPARE(eval("3(x+1)", 1.0), 6.0);
    QCOMPARE(eval("(x-1)(x+1)", 3.0), 8.0);
    QVERIFY(std::abs(eval("2sen(x)", M_PI / 2) - 2.0) < 1e-12);
    QCOMPARE(eval(QString::fromUtf8("x² − 1"), 3.0), 8.0);
    QCOMPARE(eval(QString::fromUtf8("2·x ÷ 4"), 6.0), 3.0);
    QCOMPARE(eval(QString::fromUtf8("√x"), 9.0), 3.0);
    QVERIFY(std::abs(eval(QString::fromUtf8("tg(π/4)"), 0.0) - 1.0) < 1e-12);
    QCOMPARE(eval("exp(0)", 0.0), 1.0);   // "exp" não vira "e" · "xp"
}

void SceneTest::expressionErrors()
{
    Expression e;
    QString error;
    QVERIFY(!e.parse("2 +", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!e.parse("foo(x)", &error));
    QVERIFY(!e.parse("(x + 1", &error));
    QVERIFY(!e.parse("x $ 2", &error));
    QVERIFY(!e.isValid());
}

void SceneTest::ticksAndFormat()
{
    QCOMPARE(chart::niceStep(-3, 5, 8), 1.0);
    QCOMPARE(chart::niceStep(0, 100, 5), 20.0);
    QCOMPARE(chart::niceStep(-1, 1, 4), 0.5);
    const std::vector<double> t = chart::ticks(-3.0, 5.0, 2.0);
    QCOMPARE(t, (std::vector<double>{-2.0, 0.0, 2.0, 4.0}));
    QCOMPARE(chart::format(0.5, 0.5), QString("0,5"));
    QCOMPARE(chart::format(-4.0, 2.0), QString("-4"));
    QCOMPARE(chart::format(1e-17, 1.0), QString("0"));
}

void SceneTest::autoRangeIncludesZero()
{
    Expression square;
    QVERIFY(square.parse("x^2 + 5"));
    double yMin = 0, yMax = 0;
    QVERIFY(chart::autoRange({&square}, -3.0, 3.0, 200, &yMin, &yMax));
    QCOMPARE(yMin, 0.0);            // o eixo x continua à vista
    QVERIFY(yMax >= 14.0 && yMax < 16.0);

    Expression undefined;
    QVERIFY(undefined.parse("sqrt(x)"));
    QVERIFY(!chart::autoRange({&undefined}, -5.0, -1.0, 100, &yMin, &yMax));
}

void SceneTest::curveBreaksAtAsymptotes()
{
    chart::Frame frame;
    frame.xMin = -4;
    frame.xMax = 4;
    frame.yMin = -4;
    frame.yMax = 4;
    frame.plot = QRectF(0, 0, 80, 40);

    for (const char *text : {"1/x", "tan(x)"}) {
        Expression e;
        QVERIFY(e.parse(text));
        const std::vector<Polyline> pieces = chart::sample(e, frame, 400, 0.8);
        QVERIFY2(pieces.size() >= 2, text);   // nunca liga os dois lados da assíntota
        for (const Polyline &piece : pieces)
            for (const QPointF &p : piece)
                QVERIFY2(p.y() >= -1e-9 && p.y() <= 40.0 + 1e-9, text); // cortada na borda
    }

    Expression line;
    QVERIFY(line.parse("x / 2"));
    QCOMPARE(chart::sample(line, frame, 100, 0.8).size(), std::size_t(1));
}

void SceneTest::recordedLettersReplaceFont()
{
    HersheyFont font;
    QString error;
    QVERIFY2(font.load(":/fonts/rowmans.jhf", &error), qPrintable(error));
    SceneParams params;
    TextLayout layout(font, params);

    // Um "A" gravado pequeno e acima da linha guia, como numa captura real
    QTemporaryDir dir;
    handwriting::GlyphDatabase database(dir.path());
    handwriting::StrokeRecorder recorder;
    handwriting::CaptureGuide guide;
    guide.baselineY = 400;
    guide.unitPx = 200;
    recorder.setGuide(guide);
    const auto sample = [](double x, double y, double t) {
        handwriting::RawSample s;
        s.pos = QPointF(x, y);
        s.timeMs = t;
        return s;
    };
    recorder.begin(sample(300, 340, 0));
    recorder.add(sample(340, 240, 10));
    recorder.end(sample(380, 340, 20));   // base em y = −0,3 e altura 0,5
    recorder.begin(sample(315, 300, 100));
    recorder.end(sample(365, 300, 110));
    QVERIFY(database.addVariant(recorder.build("A", QString())));

    TextBlock fontOnly = layout.layout("AB", 4.0);
    QVERIFY(!fontOnly.runs.front().recorded);

    layout.setHandwriting(&database, 7);
    TextBlock block = layout.layout("AB", 4.0);
    QCOMPARE(block.runs.size(), std::size_t(2));
    QVERIFY(block.runs[0].recorded);      // "A" veio do banco
    QVERIFY(!block.runs[1].recorded);     // "B" não foi gravado: fonte
    QCOMPARE(block.runs[0].count, std::size_t(2));

    // Normalizado: apoiado na linha de base e com a altura da maiúscula da fonte
    double top = 0, bottom = -1e9;
    for (std::size_t s = block.runs[0].first; s < block.runs[0].first + block.runs[0].count; ++s)
        for (const QPointF &p : block.strokes[s]) {
            top = std::min(top, p.y());
            bottom = std::max(bottom, p.y());
        }
    QVERIFY2(std::abs(bottom) < 0.05, qPrintable(QString::number(bottom)));
    QVERIFY2(std::abs(top + 4.0) < 0.3, qPrintable(QString::number(top)));
    // O "B" vem depois do "A", sem sobrepor
    QVERIFY(block.strokes[block.runs[1].first].front().x() > 0.5);

    layout.setHandwriting(nullptr, 0);
    QVERIFY(!layout.layout("A", 4.0).runs.front().recorded);
}

QTEST_GUILESS_MAIN(SceneTest)
#include "tst_scene.moc"
