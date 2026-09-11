#include "Scene.h"

#include <QDebug>
#include <QJsonArray>
#include <QTimer>

#include <cmath>

namespace {

bool readNumber(const QJsonObject &object, const char *key, double *out)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble())
        return false;
    *out = value.toDouble();
    return true;
}

bool readPoint(const QJsonValue &value, QPointF *out)
{
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 2 || !array[0].isDouble() || !array[1].isDouble())
        return false;
    *out = QPointF(array[0].toDouble(), array[1].toDouble());
    return true;
}

double length(const QPointF &p)
{
    return std::hypot(p.x(), p.y());
}

} // namespace

Scene::Scene(VirtualHand &hand, const SceneParams &params, QObject *parent)
    : QObject(parent)
    , m_params(params)
    , m_geometry(params)
    , m_hand(hand)
{
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
    else if (type == "conectar")
        connectElements(command);
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
    } else {
        // Geometria construída em torno de (0,0) e depois posicionada
        double a = 0, b = 0, c = 0;
        if (shape == "circulo") {
            if (!readNumber(command, "raio", &a) || a <= 0)
                return fail("circulo precisa de 'raio' > 0");
            lines.push_back(m_geometry.circle({0, 0}, a));
            element.kind = SceneElement::Kind::Ellipse;
        } else if (shape == "elipse") {
            if (!readNumber(command, "raio_x", &a) || !readNumber(command, "raio_y", &b) || a <= 0 || b <= 0)
                return fail("elipse precisa de 'raio_x' e 'raio_y' > 0");
            lines.push_back(m_geometry.ellipse({0, 0}, a, b));
            element.kind = SceneElement::Kind::Ellipse;
        } else if (shape == "retangulo") {
            if (!readNumber(command, "largura", &a) || !readNumber(command, "altura", &b) || a <= 0 || b <= 0)
                return fail("retangulo precisa de 'largura' e 'altura' > 0");
            lines.push_back(m_geometry.rectangle({0, 0}, a, b));
        } else if (shape == "triangulo" || shape == "poligono") {
            const QJsonArray array = command.value("pontos").toArray();
            const int minPoints = 3, maxPoints = shape == "triangulo" ? 3 : 12;
            std::vector<QPointF> points;
            for (const QJsonValue &v : array) {
                QPointF p;
                if (!readPoint(v, &p))
                    return fail(QString("%1: ponto inválido em 'pontos'").arg(shape));
                points.push_back(p);
            }
            if (int(points.size()) < minPoints || int(points.size()) > maxPoints)
                return fail(QString("%1 precisa de %2 a %3 pontos").arg(shape).arg(minPoints).arg(maxPoints));
            lines.push_back(m_geometry.polygon({0, 0}, points));
        } else if (shape == "arco") {
            if (!readNumber(command, "raio", &a) || !readNumber(command, "angulo_inicio", &b)
                || !readNumber(command, "angulo_fim", &c) || a <= 0)
                return fail("arco precisa de 'raio' > 0, 'angulo_inicio' e 'angulo_fim'");
            lines.push_back(m_geometry.arc({0, 0}, a, b, c));
        } else {
            return fail(QString("forma '%1' não suportada nesta etapa").arg(shape));
        }

        const QPointF origin = placement(command, Geometry2D::bounds(lines));
        for (Polyline &pl : lines)
            for (QPointF &p : pl)
                p += origin;
    }

    std::vector<Polyline> all = lines;
    all.insert(all.end(), solid.begin(), solid.end());
    element.bounds = Geometry2D::bounds(all);
    store(command, element);
    submit(command, lines, solid);
}

void Scene::connectElements(const QJsonObject &command)
{
    const QString fromId = command.value("de").toString();
    const QString toId = command.value("ate").toString();
    if (!m_elements.contains(fromId) || !m_elements.contains(toId))
        return fail(QString("conectar: elemento '%1' ou '%2' não existe").arg(fromId, toId));

    const SceneElement &a = m_elements[fromId];
    const SceneElement &b = m_elements[toId];
    QPointF from = borderPoint(a, b.bounds.center());
    QPointF to = borderPoint(b, a.bounds.center());
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
    store(command, element);
    submit(command, lines, solid);
}

void Scene::eraseElement(const QJsonObject &command)
{
    const QString id = command.value("id").toString();
    if (!m_elements.contains(id))
        return fail(QString("apagar: elemento '%1' não existe").arg(id));
    const QRectF area = m_elements.take(id).bounds;
    m_waitingHand = true;
    m_hand.erase(area);
}

void Scene::clearAll()
{
    m_elements.clear();
    m_waitingHand = true;
    m_hand.clearBoard();
}

QPointF Scene::placement(const QJsonObject &command, const QRectF &local) const
{
    QPointF at;
    if (readPoint(command.value("em"), &at))
        return at;

    QString anchor = command.value("ancora").toString();
    if (anchor.isEmpty()) {
        qWarning().noquote() << "Posicionamento ausente ou não suportado nesta etapa (use \"em\" ou \"ancora\"); usando o centro";
        anchor = "centro";
    }

    // Horizontal: esquerda / centro / direita; vertical: topo / meio / base
    const double w = m_params.boardWidth, h = m_params.boardHeight, m = m_params.margin;
    double x = w / 2 - local.center().x();
    double y = h / 2 - local.center().y();
    if (anchor.endsWith("_esquerda"))
        x = m - local.left();
    else if (anchor.endsWith("_direita"))
        x = w - m - local.right();
    if (anchor.startsWith("topo"))
        y = m - local.top();
    else if (anchor.startsWith("base"))
        y = h - m - local.bottom();

    static const QStringList known = {"topo_esquerda", "topo_centro", "topo_direita", "meio_esquerda", "centro",
                                      "meio_direita", "base_esquerda", "base_centro", "base_direita"};
    if (!known.contains(anchor))
        qWarning().noquote() << "Âncora desconhecida:" << anchor << "- usando o centro";
    return known.contains(anchor) ? QPointF(x, y) : QPointF(w / 2 - local.center().x(), h / 2 - local.center().y());
}

bool Scene::endpoint(const QJsonValue &value, QPointF *point, const SceneElement **element) const
{
    if (readPoint(value, point))
        return true;
    const auto it = m_elements.constFind(value.toString());
    if (!value.isString() || it == m_elements.constEnd())
        return false;
    *element = &it.value();
    *point = it->bounds.center();
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
        // Raio da elipse na direção de d
        const double cs = d.x() / len, sn = d.y() / len;
        const double r = rx * ry / std::sqrt(ry * ry * cs * cs + rx * rx * sn * sn);
        return c + d / len * r;
    }
    // Caixa: primeira parede atingida pelo raio
    const double tx = d.x() != 0.0 ? rx / std::abs(d.x()) : 1e9;
    const double ty = d.y() != 0.0 ? ry / std::abs(d.y()) : 1e9;
    return c + d * std::min(tx, ty);
}

void Scene::store(const QJsonObject &command, const SceneElement &element)
{
    const QString id = command.value("id").toString();
    if (id.isEmpty())
        return;
    if (m_elements.contains(id))
        qWarning().noquote() << "Id repetido:" << id << "- o elemento anterior foi substituído";
    m_elements.insert(id, element);
}

void Scene::submit(const QJsonObject &command, const std::vector<Polyline> &lines, const std::vector<Polyline> &solid)
{
    const QString style = command.value("estilo").toString("solido");
    const LineStyle lineStyle = style == "tracejado" ? LineStyle::Dashed
                              : style == "pontilhado" ? LineStyle::Dotted
                                                      : LineStyle::Solid;
    if (style != "solido" && style != "tracejado" && style != "pontilhado")
        qWarning().noquote() << "Estilo desconhecido:" << style << "- usando sólido";

    const QString pressure = command.value("pressao").toString("normal");
    const PressureLevel level = pressure == "leve"  ? PressureLevel::Light
                              : pressure == "forte" ? PressureLevel::Strong
                                                    : PressureLevel::Normal;

    std::vector<Polyline> strokes = m_geometry.styled(lines, lineStyle);
    strokes.insert(strokes.end(), solid.begin(), solid.end());
    m_waitingHand = true;
    m_hand.draw(strokes, level);
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
