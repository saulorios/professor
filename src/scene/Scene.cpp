#include "Scene.h"
#include "JsonHelpers.h"

#include <QDebug>
#include <QJsonArray>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kSqrt2 = 1.4142135623730951;

// "pressao": "leve" | "normal" (padrão) | "forte"
PressureLevel pressureLevel(const QJsonObject &command)
{
    const QString pressure = command.value("pressao").toString("normal");
    if (pressure != "leve" && pressure != "normal" && pressure != "forte")
        qWarning().noquote() << "Pressão desconhecida:" << pressure << "- usando normal";
    return pressure == "leve"  ? PressureLevel::Light
         : pressure == "forte" ? PressureLevel::Strong
                               : PressureLevel::Normal;
}

void translate(std::vector<Polyline> &lines, const QPointF &offset)
{
    for (Polyline &pl : lines)
        for (QPointF &p : pl)
            p += offset;
}

void translate(std::vector<HandStroke> &strokes, const QPointF &offset)
{
    for (HandStroke &stroke : strokes)
        for (QPointF &p : stroke.points)
            p += offset;
}

std::vector<Polyline> polylinesOf(const std::vector<HandStroke> &strokes)
{
    std::vector<Polyline> lines;
    lines.reserve(strokes.size());
    for (const HandStroke &stroke : strokes)
        lines.push_back(stroke.points);
    return lines;
}

double length(const QPointF &p)
{
    return std::hypot(p.x(), p.y());
}

// Centro médio de uma polilinha (usado para achar o "lado de fora" do objeto)
QPointF centroid(const Polyline &points)
{
    QPointF sum;
    for (const QPointF &p : points)
        sum += p;
    return points.empty() ? sum : sum / double(points.size());
}

// Parâmetros do texto em cursiva: espaçamento próprio e sem os pares de
// kerning, que foram escolhidos para a fonte normal
SceneParams cursiveParams(const SceneParams &params)
{
    SceneParams cursive = params;
    cursive.tracking = params.cursiveTracking;
    cursive.kerning = 0.0;
    return cursive;
}

// Até onde o raio que sai de `origin` na direção `direction` ainda está dentro
// do polígono (usado para jogar o rótulo para fora da silhueta do objeto)
double exitDistance(const Polyline &polygon, const QPointF &origin, const QPointF &direction)
{
    double exit = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const QPointF a = polygon[i];
        const QPointF edge = polygon[(i + 1) % polygon.size()] - a;
        const double denominator = direction.x() * edge.y() - direction.y() * edge.x();
        if (std::abs(denominator) < 1e-12)
            continue;
        const QPointF d = a - origin;
        const double t = (d.x() * edge.y() - d.y() * edge.x()) / denominator;
        const double s = (d.x() * direction.y() - d.y() * direction.x()) / denominator;
        if (t > 0.0 && s >= 0.0 && s <= 1.0)
            exit = std::max(exit, t);
    }
    return exit;
}

} // namespace

Scene::Scene(VirtualHand &hand, const SceneParams &params, QObject *parent)
    : QObject(parent)
    , m_params(params)
    , m_geometry(params)
    , m_layout(params)
    , m_textLayout(m_font, params)
    , m_cursiveLayout(m_cursiveFont, cursiveParams(params))
    , m_builder3D(params, m_layout, m_textLayout)
    , m_hand(hand)
{
    QString error;
    if (!m_font.load(m_params.fontPath, &error))
        qWarning().noquote() << "Fonte Hershey não carregada:" << error;
    if (!m_cursiveFont.load(m_params.cursiveFontPath, &error, false))
        qWarning().noquote() << "Fonte cursiva não carregada:" << error;

    connect(&m_hand, &VirtualHand::finished, this, [this] {
        if (!m_waitingHand)
            return;
        m_waitingHand = false;
        emit finished();
    });
}

void Scene::execute(const QJsonObject &command)
{
    const QString type = command.value("tipo").toString();
    if (type == "forma")
        drawShape(command);
    else if (type == "escrever")
        writeText(command);
    else if (type == "conectar")
        connectElements(command);
    else if (type == "destacar")
        highlight(command);
    else if (type == "traco_livre")
        drawFreeStroke(command);
    else if (type == "objeto_3d")
        drawObject(command);
    else if (type == "rotular")
        labelVertex(command);
    else if (type == "cotar")
        dimensionEdge(command);
    else if (type == "apagar")
        eraseElement(command);
    else if (type == "limpar")
        clearAll();
    else
        fail(QString("tipo '%1' não é tratado pela cena").arg(type));
}

void Scene::reset()
{
    ++m_generation;
    m_waitingHand = false;
    m_hand.cancel();
    m_elements.clear();
    m_objects.clear();
    emit elementsChanged();
}

SceneDebugGeometry Scene::debugGeometry() const
{
    SceneDebugGeometry geometry;
    for (const Object3DInfo &object : m_objects) {
        geometry.hiddenLines.insert(geometry.hiddenLines.end(), object.hiddenLines.begin(),
                                    object.hiddenLines.end());
        geometry.vanishingLines.insert(geometry.vanishingLines.end(), object.vanishingLines.begin(),
                                       object.vanishingLines.end());
        geometry.vanishingPoints.insert(geometry.vanishingPoints.end(), object.vanishingPoints.begin(),
                                        object.vanishingPoints.end());
    }
    return geometry;
}

QRectF Scene::bounds(const QString &id) const
{
    const SceneElement *element = Layout::find(m_elements, id);
    return element ? element->bounds : QRectF();
}

void Scene::drawShape(const QJsonObject &command)
{
    const QString shape = command.value("forma").toString();
    std::vector<Polyline> lines;
    std::vector<Polyline> solid;
    SceneElement element;

    if (shape == "linha" || shape == "seta") {
        QPointF from, to;
        const SceneElement *fromElement = nullptr;
        const SceneElement *toElement = nullptr;
        if (!endpoint(command.value("de"), &from, &fromElement) || !endpoint(command.value("ate"), &to, &toElement))
            return fail(QString("%1 precisa de 'de' e 'ate' ([x,y] ou id existente)").arg(shape));
        // Pontas ligadas a elementos saem da borda deles, com uma pequena folga
        const QPointF rawFrom = from, rawTo = to;
        if (fromElement)
            from = borderPoint(*fromElement, rawTo);
        if (toElement)
            to = borderPoint(*toElement, rawFrom);
        const QPointF d = to - from;
        const double len = length(d);
        if (len <= 0.0)
            return fail(QString("%1 com comprimento zero").arg(shape));
        const QPointF unit = d / len;
        if (fromElement)
            from += unit * m_params.connectGap;
        if (toElement)
            to -= unit * m_params.connectGap;

        lines.push_back(m_geometry.line(from, to));
        if (shape == "seta")
            solid.push_back(m_geometry.arrowHead(from, to));
        element.obstacle = false; // um traço diagonal não ocupa a sua bounding box
    } else {
        // Geometria construída em torno de (0,0), que é o ponto de referência da forma
        double a = 0, b = 0, c = 0;
        if (shape == "circulo") {
            if (!json::readNumber(command, "raio", &a) || a <= 0)
                return fail("circulo precisa de 'raio' > 0");
            lines.push_back(m_geometry.circle({0, 0}, a));
            element.kind = SceneElement::Kind::Ellipse;
        } else if (shape == "elipse") {
            if (!json::readNumber(command, "raio_x", &a) || !json::readNumber(command, "raio_y", &b) || a <= 0 || b <= 0)
                return fail("elipse precisa de 'raio_x' e 'raio_y' > 0");
            lines.push_back(m_geometry.ellipse({0, 0}, a, b));
            element.kind = SceneElement::Kind::Ellipse;
        } else if (shape == "retangulo") {
            if (!json::readNumber(command, "largura", &a) || !json::readNumber(command, "altura", &b) || a <= 0 || b <= 0)
                return fail("retangulo precisa de 'largura' e 'altura' > 0");
            lines.push_back(m_geometry.rectangle({0, 0}, a, b));
        } else if (shape == "triangulo" || shape == "poligono") {
            const QJsonArray array = command.value("pontos").toArray();
            const int minPoints = 3, maxPoints = shape == "triangulo" ? 3 : 12;
            std::vector<QPointF> points;
            for (const QJsonValue &v : array) {
                QPointF p;
                if (!json::readPoint(v, &p))
                    return fail(QString("%1: ponto inválido em 'pontos'").arg(shape));
                points.push_back(p);
            }
            if (int(points.size()) < minPoints || int(points.size()) > maxPoints)
                return fail(QString("%1 precisa de %2 a %3 pontos").arg(shape).arg(minPoints).arg(maxPoints));
            lines.push_back(m_geometry.polygon({0, 0}, points));
        } else if (shape == "arco") {
            if (!json::readNumber(command, "raio", &a) || !json::readNumber(command, "angulo_inicio", &b)
                || !json::readNumber(command, "angulo_fim", &c) || a <= 0)
                return fail("arco precisa de 'raio' > 0, 'angulo_inicio' e 'angulo_fim'");
            lines.push_back(m_geometry.arc({0, 0}, a, b, c));
        } else {
            return fail(QString("forma '%1' não suportada nesta etapa").arg(shape));
        }
        const QPointF offset = m_layout.place(command, Geometry2D::bounds(lines), QPointF(0, 0), m_elements);
        translate(lines, offset);
        element.anchor = offset;
    }

    std::vector<Polyline> all = lines;
    all.insert(all.end(), solid.begin(), solid.end());
    element.bounds = Geometry2D::bounds(all);
    if (shape == "linha" || shape == "seta")
        element.anchor = element.bounds.center();
    store(command, element, shape);
    submit(command, lines, solid);
}

std::vector<HandStroke> Scene::writing(const QString &text, double size, bool cursive,
                                       PressureLevel pressure) const
{
    QString missing;
    std::vector<GlyphRun> runs;
    const std::vector<Polyline> raw =
        (cursive ? m_cursiveLayout : m_textLayout).layout(text, size, &missing, &runs);
    if (!missing.isEmpty())
        qWarning().noquote() << "Caracteres sem glifo ignorados:" << missing;
    return m_humanizer.apply(raw, runs, pressure, cursive);
}

void Scene::writeText(const QJsonObject &command)
{
    const QString text = command.value("texto").toString();
    if (text.trimmed().isEmpty())
        return fail("escrever precisa de 'texto'");
    // "fonte": "normal" (padrão) ou "cursiva"
    const QString fontName = command.value("fonte").toString("normal");
    const bool cursive = fontName == "cursiva";
    if (!cursive && fontName != "normal")
        qWarning().noquote() << "Fonte desconhecida:" << fontName << "- usando normal";
    if (!(cursive ? m_cursiveFont : m_font).isLoaded())
        return fail(QString("escrever: fonte %1 não carregada").arg(cursive ? "cursiva" : "normal"));
    double size = m_params.textDefaultSize;
    if (command.contains("tamanho") && (!json::readNumber(command, "tamanho", &size) || size <= 0))
        return fail("escrever: 'tamanho' deve ser um número > 0");

    std::vector<HandStroke> strokes = writing(text, size, cursive, pressureLevel(command));
    if (strokes.empty())
        return fail("escrever: nenhum caractere desenhável");

    // O ponto de referência do texto é o seu centro; a ordem dos traços
    // (letra por letra) é preservada até a mão
    const QRectF local = Geometry2D::bounds(polylinesOf(strokes));
    const QPointF offset = m_layout.place(command, local, local.center(), m_elements);
    translate(strokes, offset);

    SceneElement element;
    element.bounds = Geometry2D::bounds(polylinesOf(strokes));
    element.anchor = local.center() + offset;
    store(command, element, QString("\"%1\"").arg(text));
    m_waitingHand = true;
    m_hand.draw(strokes, Motion::Writing);
}

void Scene::connectElements(const QJsonObject &command)
{
    const QString fromId = command.value("de").toString();
    const QString toId = command.value("ate").toString();
    const SceneElement *a = Layout::find(m_elements, fromId);
    const SceneElement *b = Layout::find(m_elements, toId);
    if (!a || !b)
        return fail(QString("conectar: elemento '%1' ou '%2' não existe").arg(fromId, toId));

    // Liga as bordas (não os centros), com uma pequena folga
    QPointF from = borderPoint(*a, b->bounds.center());
    QPointF to = borderPoint(*b, a->bounds.center());
    const QPointF d = to - from;
    const double len = length(d);
    if (len <= 2.0 * m_params.connectGap)
        return fail(QString("conectar: '%1' e '%2' estão encostados").arg(fromId, toId));
    const QPointF unit = d / len;
    from += unit * m_params.connectGap;
    to -= unit * m_params.connectGap;

    std::vector<Polyline> solid;
    if (command.value("seta").toBool(false))
        solid.push_back(m_geometry.arrowHead(from, to));
    const std::vector<Polyline> lines{m_geometry.line(from, to)};

    SceneElement element;
    std::vector<Polyline> all = lines;
    all.insert(all.end(), solid.begin(), solid.end());
    element.bounds = Geometry2D::bounds(all);
    element.anchor = element.bounds.center();
    element.obstacle = false;
    store(command, element, "conectar");
    submit(command, lines, solid);
}

void Scene::highlight(const QJsonObject &command)
{
    const QString targetId = command.value("alvo").toString();
    const SceneElement *target = Layout::find(m_elements, targetId);
    if (!target)
        return fail(QString("destacar: elemento '%1' não existe").arg(targetId));

    const QString mode = command.value("modo").toString();
    const QRectF r = target->bounds;
    const double gap = m_params.highlightGap;
    std::vector<Polyline> lines;
    if (mode == "sublinhar") {
        lines.push_back(m_geometry.line({r.left(), r.bottom() + gap}, {r.right(), r.bottom() + gap}));
    } else if (mode == "circular") {
        // Elipse que passa pelos cantos da caixa (com folga): envolve o elemento todo
        const QRectF box = r.adjusted(-gap, -gap, gap, gap);
        lines.push_back(m_geometry.ellipse(box.center(), box.width() / 2 * kSqrt2, box.height() / 2 * kSqrt2));
    } else if (mode == "caixa") {
        const QRectF box = r.adjusted(-gap, -gap, gap, gap);
        lines.push_back(m_geometry.rectangle(box.center(), box.width(), box.height()));
    } else {
        return fail(QString("destacar: modo '%1' desconhecido (sublinhar, circular ou caixa)").arg(mode));
    }

    SceneElement element;
    element.bounds = Geometry2D::bounds(lines);
    element.anchor = element.bounds.center();
    element.obstacle = false;
    store(command, element, mode);
    submit(command, lines);
}

void Scene::drawFreeStroke(const QJsonObject &command)
{
    // Traço gravado do mouse ou da caneta: [x, y, pressão] e, opcionalmente, o
    // instante em segundos. Já vem em unidades da lousa, sem passar pelo layout.
    const QJsonArray points = command.value("pontos").toArray();
    std::vector<RecordedPoint> recorded;
    Polyline line;
    double previousTime = 0.0;
    for (const QJsonValue &value : points) {
        const QJsonArray point = value.toArray();
        if (point.size() < 2)
            return fail("traco_livre: cada ponto é [x, y] ou [x, y, pressao] ou [x, y, pressao, t]");
        RecordedPoint sample;
        sample.pos = QPointF(point[0].toDouble(), point[1].toDouble());
        if (point.size() > 2)
            sample.pressure = float(std::clamp(point[2].toDouble(0.6), 0.0, 1.0));
        // Sem tempo gravado, o traço sai na velocidade de escrita da mão
        previousTime = point.size() > 3 ? point[3].toDouble() * 1000.0
                                        : previousTime + (recorded.empty()
                                                              ? 0.0
                                                              : length(sample.pos - recorded.back().pos)
                                                                    / m_hand.params().writingSpeed * 1000.0);
        sample.timeMs = previousTime;
        recorded.push_back(sample);
        line.push_back(sample.pos);
    }
    if (recorded.size() < 2)
        return fail("traco_livre precisa de pelo menos 2 pontos");

    SceneElement element;
    element.bounds = Geometry2D::bounds({line});
    element.anchor = element.bounds.center();
    element.obstacle = false; // um rabisco não ocupa a sua bounding box inteira
    store(command, element, "traço livre");
    m_waitingHand = true;
    m_hand.drawRecorded(recorded);
}

void Scene::drawObject(const QJsonObject &command)
{
    std::vector<ObjectStroke> strokes;
    Object3DInfo info;
    QString error;
    if (!m_builder3D.build(command, m_elements, pressureLevel(command), &strokes, &info, &error))
        return fail(error);

    info.id = command.value("id").toString();
    if (!info.id.isEmpty())
        forgetObject(info.id);
    m_objects.push_back(info);

    SceneElement element;
    element.bounds = info.bounds;
    element.anchor = info.bounds.center();
    store(command, element, "objeto_3d");

    // Cada traço leva a sua pressão (as arestas ocultas são leves)
    std::vector<Polyline> lines;
    std::vector<PressureLevel> pressures;
    lines.reserve(strokes.size());
    pressures.reserve(strokes.size());
    for (const ObjectStroke &stroke : strokes) {
        lines.push_back(stroke.points);
        pressures.push_back(stroke.pressure);
    }
    m_waitingHand = true;
    m_hand.draw(lines, pressures);
}

void Scene::labelVertex(const QJsonObject &command)
{
    const QString targetId = command.value("alvo").toString();
    const Object3DInfo *object = findObject(targetId);
    if (!object)
        return fail(QString("rotular: objeto 3D '%1' não existe").arg(targetId));

    // "vertice" pode vir como "parte.nome" quando o objeto tem várias partes
    const QString wanted = command.value("vertice").toString();
    const int dot = wanted.indexOf('.');
    const QString part = dot > 0 ? wanted.left(dot) : QString();
    const QString name = dot > 0 ? wanted.mid(dot + 1) : wanted;
    const auto found = std::find_if(object->vertices.begin(), object->vertices.end(),
                                    [&](const Object3DVertex &v) {
                                        return v.name == name && (part.isEmpty() || v.part == part);
                                    });
    if (found == object->vertices.end())
        return fail(QString("rotular: vértice '%1' não existe em '%2'").arg(wanted, targetId));

    const QString text = command.value("texto").toString();
    if (text.trimmed().isEmpty())
        return fail("rotular precisa de 'texto'");
    std::vector<HandStroke> strokes = writing(text, m_params.labelSize, false, pressureLevel(command));
    if (strokes.empty())
        return fail("rotular: nenhum caractere desenhável");

    // Deslocado para fora do objeto, na direção que sai do centro da silhueta
    QPointF direction = found->at - centroid(object->hull);
    const double len = length(direction);
    direction = len > 0.0 ? direction / len : QPointF(0.0, -1.0);
    const QRectF local = Geometry2D::bounds(polylinesOf(strokes));
    const double reach = std::abs(direction.x()) * local.width() / 2 + std::abs(direction.y()) * local.height() / 2;
    // Vértices no meio do desenho (como a quina mais próxima de um cubo) exigem
    // sair da silhueta antes de afastar o texto
    const double out = exitDistance(object->hull, found->at, direction);
    const QPointF at = found->at + direction * (out + m_params.labelGap + reach);

    QJsonObject placed = command;
    placed["em"] = QJsonArray{at.x(), at.y()};
    const QPointF offset = m_layout.place(placed, local, local.center(), m_elements);
    translate(strokes, offset);

    SceneElement element;
    element.bounds = Geometry2D::bounds(polylinesOf(strokes));
    element.anchor = element.bounds.center();
    store(command, element, QString("rótulo \"%1\"").arg(text));
    m_waitingHand = true;
    m_hand.draw(strokes, Motion::Writing);
}

void Scene::dimensionEdge(const QJsonObject &command)
{
    const QString targetId = command.value("alvo").toString();
    const Object3DInfo *object = findObject(targetId);
    if (!object)
        return fail(QString("cotar: objeto 3D '%1' não existe").arg(targetId));

    const QString axisName = command.value("aresta").toString();
    const int axis = axisName == "largura" ? 0 : axisName == "altura" ? 1 : axisName == "profundidade" ? 2 : -1;
    if (axis < 0)
        return fail(QString("cotar: aresta '%1' desconhecida (largura, altura ou profundidade)").arg(axisName));

    // Aresta visível mais afastada do miolo do desenho: a cota fica por fora
    const Object3DEdge *chosen = nullptr;
    for (const Object3DEdge &edge : object->edges) {
        if (edge.axis != axis)
            continue;
        if (!chosen) {
            chosen = &edge;
            continue;
        }
        if (chosen->visible != edge.visible) {
            if (edge.visible)
                chosen = &edge;
            continue;
        }
        const QPointF a = (edge.a + edge.b) / 2.0, b = (chosen->a + chosen->b) / 2.0;
        const bool better = axis == 1 ? (a.x() < b.x() - 1e-6 || (std::abs(a.x() - b.x()) < 1e-6 && a.y() > b.y()))
                          : axis == 0 ? (a.y() > b.y() + 1e-6 || (std::abs(a.y() - b.y()) < 1e-6 && a.x() < b.x()))
                                      : (a.y() > b.y() + 1e-6 || (std::abs(a.y() - b.y()) < 1e-6 && a.x() > b.x()));
        if (better)
            chosen = &edge;
    }
    if (!chosen)
        return fail(QString("cotar: '%1' não tem aresta de %2").arg(targetId, axisName));

    QPointF from = chosen->a, to = chosen->b;
    if (to.x() < from.x() || (std::abs(to.x() - from.x()) < 1e-6 && to.y() < from.y()))
        std::swap(from, to);
    const QPointF along = to - from;
    const double len = length(along);
    if (len <= 0.0)
        return fail("cotar: aresta de comprimento zero na projeção");
    const QPointF unit = along / len;
    QPointF normal(-unit.y(), unit.x());
    const QPointF middle = (from + to) / 2.0;
    if (QPointF::dotProduct(normal, middle - centroid(object->hull)) < 0.0)
        normal = -normal;

    const QPointF shift = normal * m_params.dimensionOffset;
    const QPointF tick = normal * (m_params.dimensionTick / 2.0);
    std::vector<Polyline> lines{{from + shift, to + shift},
                                {from + shift - tick, from + shift + tick},
                                {to + shift - tick, to + shift + tick}};

    // Texto no meio da cota, do lado de fora
    const QString text = command.contains("texto") ? command.value("texto").toString()
                                                   : QString::number(chosen->length3D, 'g', 3);
    std::vector<HandStroke> strokes;
    for (const Polyline &line : lines)
        strokes.push_back({line, pressureLevel(command), 1.0, 1.0, 0.0});

    std::vector<HandStroke> label = writing(text, m_params.dimensionTextSize, false, pressureLevel(command));
    if (!label.empty()) {
        const QRectF local = Geometry2D::bounds(polylinesOf(label));
        const double reach = std::abs(normal.x()) * local.width() / 2 + std::abs(normal.y()) * local.height() / 2;
        const QPointF at = middle + shift + normal * (m_params.dimensionTextGap + reach);
        translate(label, at - local.center());
        strokes.insert(strokes.end(), label.begin(), label.end());
    }

    SceneElement element;
    element.bounds = Geometry2D::bounds(polylinesOf(strokes));
    element.anchor = element.bounds.center();
    store(command, element, QString("cota \"%1\"").arg(text));
    m_waitingHand = true;
    m_hand.draw(strokes);
}

const Object3DInfo *Scene::findObject(const QString &id) const
{
    if (id.isEmpty())
        return nullptr;
    for (auto it = m_objects.rbegin(); it != m_objects.rend(); ++it)
        if (it->id == id)
            return &*it;
    return nullptr;
}

void Scene::forgetObject(const QString &id)
{
    m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
                                   [&](const Object3DInfo &object) { return object.id == id; }),
                    m_objects.end());
}

void Scene::eraseElement(const QJsonObject &command)
{
    const QString id = command.value("id").toString();
    const SceneElement *element = Layout::find(m_elements, id);
    if (!element)
        return fail(QString("apagar: elemento '%1' não existe").arg(id));
    const QRectF area = element->bounds;
    m_elements.erase(m_elements.begin() + (element - m_elements.data()));
    forgetObject(id);
    emit elementsChanged();
    m_waitingHand = true;
    m_hand.erase(area);
}

void Scene::clearAll()
{
    m_elements.clear();
    m_objects.clear();
    emit elementsChanged();
    m_waitingHand = true;
    m_hand.clearBoard();
}

bool Scene::endpoint(const QJsonValue &value, QPointF *point, const SceneElement **element) const
{
    if (json::readPoint(value, point))
        return true;
    const SceneElement *found = value.isString() ? Layout::find(m_elements, value.toString()) : nullptr;
    if (!found)
        return false;
    *element = found;
    *point = found->bounds.center();
    return true;
}

QPointF Scene::borderPoint(const SceneElement &element, const QPointF &toward) const
{
    const QPointF c = element.bounds.center();
    const QPointF d = toward - c;
    const double len = length(d);
    if (len <= 0.0)
        return c;
    const double rx = element.bounds.width() / 2, ry = element.bounds.height() / 2;

    if (element.kind == SceneElement::Kind::Ellipse) {
        // Elipse inscrita na bounding box: raio na direção de d
        const double cs = d.x() / len, sn = d.y() / len;
        const double r = rx * ry / std::sqrt(ry * ry * cs * cs + rx * rx * sn * sn);
        return c + d / len * r;
    }
    // Caixa: primeira parede atingida pelo raio
    const double tx = d.x() != 0.0 ? rx / std::abs(d.x()) : 1e9;
    const double ty = d.y() != 0.0 ? ry / std::abs(d.y()) : 1e9;
    return c + d * std::min(tx, ty);
}

void Scene::store(const QJsonObject &command, SceneElement element, const QString &label)
{
    element.id = command.value("id").toString();
    element.label = element.id.isEmpty() ? label : element.id;
    if (!element.id.isEmpty()) {
        const auto old = std::find_if(m_elements.begin(), m_elements.end(),
                                      [&](const SceneElement &e) { return e.id == element.id; });
        if (old != m_elements.end()) {
            qWarning().noquote() << "Id repetido:" << element.id << "- o elemento anterior foi substituído";
            m_elements.erase(old);
        }
    }
    m_elements.push_back(element);
    emit elementsChanged();
}

void Scene::submit(const QJsonObject &command, const std::vector<Polyline> &lines, const std::vector<Polyline> &solid)
{
    const QString style = command.value("estilo").toString("solido");
    const LineStyle lineStyle = style == "tracejado" ? LineStyle::Dashed
                              : style == "pontilhado" ? LineStyle::Dotted
                                                      : LineStyle::Solid;
    if (style != "solido" && style != "tracejado" && style != "pontilhado")
        qWarning().noquote() << "Estilo desconhecido:" << style << "- usando sólido";

    std::vector<Polyline> strokes = m_geometry.styled(lines, lineStyle);
    strokes.insert(strokes.end(), solid.begin(), solid.end());
    m_waitingHand = true;
    m_hand.draw(strokes, pressureLevel(command));
}

void Scene::finishLater()
{
    QTimer::singleShot(0, this, [this, generation = m_generation] {
        if (generation == m_generation)
            emit finished();
    });
}

void Scene::fail(const QString &message)
{
    qWarning().noquote() << "Comando ignorado:" << message;
    finishLater();
}
