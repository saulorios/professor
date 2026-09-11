#include "Scene.h"
#include "JsonHelpers.h"

#include <QDebug>
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

double length(const QPointF &p)
{
    return std::hypot(p.x(), p.y());
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

} // namespace

Scene::Scene(VirtualHand &hand, const SceneParams &params, QObject *parent)
    : QObject(parent)
    , m_params(params)
    , m_geometry(params)
    , m_layout(params)
    , m_textLayout(m_font, params)
    , m_cursiveLayout(m_cursiveFont, cursiveParams(params))
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
    emit elementsChanged();
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

    QString missing;
    std::vector<Polyline> strokes = (cursive ? m_cursiveLayout : m_textLayout).layout(text, size, &missing);
    if (!missing.isEmpty())
        qWarning().noquote() << "Caracteres sem glifo ignorados:" << missing;
    if (strokes.empty())
        return fail("escrever: nenhum caractere desenhável");

    // O ponto de referência do texto é o seu centro; a ordem dos traços
    // (letra por letra) é preservada até a mão
    const QRectF local = Geometry2D::bounds(strokes);
    const QPointF offset = m_layout.place(command, local, local.center(), m_elements);
    translate(strokes, offset);

    SceneElement element;
    element.bounds = Geometry2D::bounds(strokes);
    element.anchor = local.center() + offset;
    store(command, element, QString("\"%1\"").arg(text));
    m_waitingHand = true;
    m_hand.draw(strokes, pressureLevel(command), Motion::Writing);
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

void Scene::eraseElement(const QJsonObject &command)
{
    const QString id = command.value("id").toString();
    const SceneElement *element = Layout::find(m_elements, id);
    if (!element)
        return fail(QString("apagar: elemento '%1' não existe").arg(id));
    const QRectF area = element->bounds;
    m_elements.erase(m_elements.begin() + (element - m_elements.data()));
    emit elementsChanged();
    m_waitingHand = true;
    m_hand.erase(area);
}

void Scene::clearAll()
{
    m_elements.clear();
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
