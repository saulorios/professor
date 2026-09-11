#include "Layout.h"
#include "JsonHelpers.h"

#include <QDebug>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kPi = 3.141592653589793;

const QStringList kAnchors = {"topo_esquerda", "topo_centro", "topo_direita", "meio_esquerda", "centro",
                              "meio_direita", "base_esquerda", "base_centro", "base_direita"};
const QStringList kSides = {"abaixo_de", "acima_de", "direita_de", "esquerda_de"};

// Descrição do elemento para as mensagens de log
QString describe(const QJsonObject &command)
{
    const QString id = command.value("id").toString();
    if (!id.isEmpty())
        return QString("\"%1\"").arg(id);
    const QString type = command.value("tipo").toString();
    if (type == "escrever")
        return QString("escrever \"%1\"").arg(command.value("texto").toString());
    return QString("%1 %2").arg(type, command.value("forma").toString()).trimmed();
}

// Menor avanço t >= 0 na direção `d` (unitária) que separa `box` de `obstacle`
double separation(const QRectF &box, const QRectF &obstacle, const QPointF &d)
{
    constexpr double kEpsilon = 1e-9;
    double t = std::numeric_limits<double>::infinity();
    if (d.x() > kEpsilon)
        t = std::min(t, (obstacle.right() - box.left()) / d.x());
    else if (d.x() < -kEpsilon)
        t = std::min(t, (box.right() - obstacle.left()) / -d.x());
    if (d.y() > kEpsilon)
        t = std::min(t, (obstacle.bottom() - box.top()) / d.y());
    else if (d.y() < -kEpsilon)
        t = std::min(t, (box.bottom() - obstacle.top()) / -d.y());
    return std::isfinite(t) ? std::max(t, 0.0) : 0.0;
}

bool collides(const QRectF &box, const std::vector<SceneElement> &elements, const SceneElement *exclude)
{
    return std::any_of(elements.begin(), elements.end(), [&](const SceneElement &e) {
        return e.obstacle && &e != exclude && e.bounds.intersects(box);
    });
}

} // namespace

Layout::Layout(const SceneParams &params)
    : m_params(params)
{
}

QRectF Layout::usableArea() const
{
    const double m = m_params.margin;
    return QRectF(m, m, m_params.boardWidth - 2 * m, m_params.boardHeight - m_params.captionBandHeight - 2 * m);
}

const SceneElement *Layout::find(const std::vector<SceneElement> &elements, const QString &id)
{
    if (id.isEmpty())
        return nullptr;
    for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        if (it->id == id)
            return &*it;
    return nullptr;
}

QPointF Layout::place(const QJsonObject &command, const QRectF &local, const QPointF &localAnchor,
                      const std::vector<SceneElement> &elements) const
{
    const QString who = describe(command);
    QPointF offset;
    QPointF direction(0.0, 1.0);          // para onde afastar em caso de colisão
    bool avoidCollisions = true;
    const SceneElement *exclude = nullptr; // referência que o elemento pode tocar de propósito

    // Âncora "centro" quando a referência não existe
    auto fallbackToCenter = [&](const QString &reason) {
        qWarning().noquote() << QString("Layout: %1 — %2; usando \"centro\"").arg(who, reason);
        offset = anchorOffset("centro", local, &direction);
    };

    QString side;
    for (const QString &key : kSides)
        if (command.contains(key)) {
            side = key;
            break;
        }

    QPointF at;
    if (json::readPoint(command.value("em"), &at)) {
        // Posição absoluta pedida explicitamente: o ponto de referência vai para lá
        offset = at - localAnchor;
        avoidCollisions = false;
    } else if (command.contains("em_centro_de")) {
        const QString id = command.value("em_centro_de").toString();
        if (const SceneElement *ref = find(elements, id)) {
            offset = ref->bounds.center() - local.center();
            avoidCollisions = false; // fica dentro do outro de propósito
        } else {
            fallbackToCenter(QString("em_centro_de \"%1\" não existe").arg(id));
        }
    } else if (command.contains("relativo_a")) {
        const QString id = command.value("relativo_a").toString();
        if (const SceneElement *ref = find(elements, id)) {
            double angle = 0.0, distance = 0.0;
            const bool hasDistance = json::readNumber(command, "distancia", &distance);
            const bool hasAngle = json::readNumber(command, "angulo", &angle);
            // Com distância 0 o ângulo não importa (ex.: arco centrado na referência)
            if (!hasDistance || (!hasAngle && distance != 0.0))
                qWarning().noquote() << QString("Layout: %1 — relativo_a sem \"angulo\" ou \"distancia\"; usando 0").arg(who);
            // 0° = direita, positivo = anti-horário (Y da lousa para baixo)
            const double a = angle * kPi / 180.0;
            const QPointF unit(std::cos(a), -std::sin(a));
            offset = ref->anchor + unit * distance - localAnchor;
            if (distance > 0.0)
                direction = unit;
            exclude = ref;
        } else {
            fallbackToCenter(QString("relativo_a \"%1\" não existe").arg(id));
        }
    } else if (!side.isEmpty()) {
        const QString id = command.value(side).toString();
        if (const SceneElement *ref = find(elements, id)) {
            double margin = m_params.relativeMargin;
            if (command.contains("margem") && !json::readNumber(command, "margem", &margin))
                qWarning().noquote() << QString("Layout: %1 — \"margem\" inválida; usando %2").arg(who).arg(m_params.relativeMargin);
            QString align = command.value("alinhar").toString("centro");
            if (align != "inicio" && align != "centro" && align != "fim") {
                qWarning().noquote() << QString("Layout: %1 — alinhar \"%2\" desconhecido; usando \"centro\"").arg(who, align);
                align = "centro";
            }
            const QRectF r = ref->bounds;
            if (side == "abaixo_de" || side == "acima_de") {
                offset.setY(side == "abaixo_de" ? r.bottom() + margin - local.top() : r.top() - margin - local.bottom());
                offset.setX(align == "inicio" ? r.left() - local.left()
                            : align == "fim"  ? r.right() - local.right()
                                              : r.center().x() - local.center().x());
                direction = QPointF(0.0, side == "abaixo_de" ? 1.0 : -1.0);
            } else {
                offset.setX(side == "direita_de" ? r.right() + margin - local.left() : r.left() - margin - local.right());
                offset.setY(align == "inicio" ? r.top() - local.top()
                            : align == "fim"  ? r.bottom() - local.bottom()
                                              : r.center().y() - local.center().y());
                direction = QPointF(side == "direita_de" ? 1.0 : -1.0, 0.0);
            }
        } else {
            fallbackToCenter(QString("%1 \"%2\" não existe").arg(side, id));
        }
    } else {
        QString anchor = command.value("ancora").toString();
        if (anchor.isEmpty()) {
            qWarning().noquote() << QString("Layout: %1 — sem posicionamento; usando \"centro\"").arg(who);
            anchor = "centro";
        } else if (!kAnchors.contains(anchor)) {
            qWarning().noquote() << QString("Layout: %1 — âncora \"%2\" desconhecida; usando \"centro\"").arg(who, anchor);
            anchor = "centro";
        }
        offset = anchorOffset(anchor, local, &direction);
    }

    // Dentro da área útil e sem sobrepor outros elementos
    const QRectF initial = local.translated(offset);
    QRectF box = initial;
    bool pushed = false;
    auto keepInside = [&] {
        const QRectF inside = pushInside(box);
        if (inside != box) {
            pushed = true;
            box = inside;
        }
    };
    keepInside();

    if (avoidCollisions) {
        for (int attempt = 0; attempt < m_params.maxCollisionAttempts; ++attempt) {
            double needed = 0.0;
            for (const SceneElement &e : elements)
                if (e.obstacle && &e != exclude && e.bounds.intersects(box))
                    needed = std::max(needed, separation(box, e.bounds, direction));
            if (needed <= 0.0)
                break;
            box.translate(direction * (needed + m_params.collisionGap));
            keepInside();
        }
        if (collides(box, elements, exclude))
            qWarning().noquote() << QString("Layout: %1 — ainda sobrepõe outro elemento após %2 tentativas; aceito")
                                        .arg(who).arg(m_params.maxCollisionAttempts);
    }
    if (pushed)
        qWarning().noquote() << QString("Layout: %1 — saía da área útil da lousa; empurrado para dentro").arg(who);

    return offset + (box.topLeft() - initial.topLeft());
}

QPointF Layout::anchorOffset(const QString &anchor, const QRectF &local, QPointF *direction) const
{
    // Horizontal: esquerda / centro / direita; vertical: topo / meio / base (na área útil)
    const QRectF area = usableArea();
    double x = area.center().x() - local.center().x();
    double y = area.center().y() - local.center().y();
    if (anchor.endsWith("_esquerda"))
        x = area.left() - local.left();
    else if (anchor.endsWith("_direita"))
        x = area.right() - local.right();
    *direction = QPointF(0.0, 1.0);
    if (anchor.startsWith("topo")) {
        y = area.top() - local.top();
    } else if (anchor.startsWith("base")) {
        y = area.bottom() - local.bottom();
        *direction = QPointF(0.0, -1.0);
    }
    return QPointF(x, y);
}

QRectF Layout::pushInside(const QRectF &box) const
{
    const QRectF area = usableArea();
    double dx = 0.0, dy = 0.0;
    if (box.width() > area.width() || box.left() < area.left())
        dx = area.left() - box.left();
    else if (box.right() > area.right())
        dx = area.right() - box.right();
    if (box.height() > area.height() || box.top() < area.top())
        dy = area.top() - box.top();
    else if (box.bottom() > area.bottom())
        dy = area.bottom() - box.bottom();
    return box.translated(dx, dy);
}
