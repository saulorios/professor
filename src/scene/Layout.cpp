#include "Layout.h"
#include "JsonHelpers.h"

#include <QDebug>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kEpsilon = 1e-6;

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

} // namespace

Layout::Layout(const SceneParams &params)
    : m_params(params)
{
    reset();
}

QRectF Layout::screenArea(int screen) const
{
    const double m = m_params.margin;
    const double top = screen * m_params.boardHeight + m;
    return QRectF(m, top, m_params.boardWidth - 2 * m, m_params.boardHeight - 2 * m);
}

QRectF Layout::canvasArea() const
{
    return QRectF(0.0, 0.0, m_params.boardWidth, canvasHeight());
}

void Layout::reset()
{
    m_screens = 1;
    m_screen = 0;
    m_column = 0;
    m_twoColumns = false;
    m_grid.reset(m_params.boardWidth, m_params.boardHeight);
    m_pen[0] = m_pen[1] = screenArea(0).top();
}

void Layout::include(const QRectF &area)
{
    if (area.isNull())
        return;
    growTo(std::max(0, int(std::floor(std::max(0.0, area.bottom() - kEpsilon) / m_params.boardHeight))));
}

void Layout::newLine()
{
    m_pen[m_column] += m_params.flowLineHeight;
}

bool Layout::setColumn(const QString &which)
{
    if (which == "unica") {
        // Volta para uma coluna só, abaixo do que já foi escrito nas duas
        m_pen[0] = m_pen[1] = std::max(m_pen[0], m_pen[1]);
        m_twoColumns = false;
        m_columnChosen = false;
        m_column = 0;
        return true;
    }
    if (which != "esquerda" && which != "direita")
        return false;
    if (!m_twoColumns) {
        // A partir daqui a tela tem duas colunas, começando na mesma altura
        m_twoColumns = true;
        m_pen[1] = m_pen[0] = std::max(m_pen[0], m_pen[1]);
    }
    m_column = which == "direita" ? 1 : 0;
    m_columnChosen = true;
    return true;
}

void Layout::newScreen()
{
    overflowScreen();
    m_column = 0;
    m_twoColumns = false;
    m_columnChosen = false;
}

void Layout::overflowScreen()
{
    growTo(m_screen + 1);
    m_screen += 1;
    m_pen[0] = m_pen[1] = screenArea(m_screen).top();
}

void Layout::growTo(int screen)
{
    if (screen < m_screens)
        return;
    m_screens = screen + 1;
    m_grid.ensureHeight(canvasHeight());
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

QRectF Layout::columnRect(int screen, int column) const
{
    const QRectF area = screenArea(screen);
    if (!m_twoColumns)
        return area;
    const double width = (area.width() - m_params.flowColumnGap) / 2.0;
    return column == 0 ? QRectF(area.left(), area.top(), width, area.height())
                       : QRectF(area.right() - width, area.top(), width, area.height());
}

int Layout::screenOf(const QRectF &box) const
{
    return std::max(0, int(std::floor(box.center().y() / m_params.boardHeight)));
}

void Layout::rebuildGrid(const std::vector<SceneElement> &elements, const SceneElement *allowed)
{
    m_grid.ensureHeight(canvasHeight());
    m_grid.clear();
    for (const SceneElement &element : elements) {
        if (&element == allowed)
            continue;
        // Linhas e setas marcam só o próprio traço; o resto marca a caixa
        if (!element.marks.empty()) {
            for (const Polyline &line : element.marks)
                m_grid.markPath(line, m_params.strokeThickness);
        } else if (element.obstacle) {
            m_grid.markBox(element.bounds);
        }
    }
}

QPointF Layout::flowOrigin(const QRectF &local, Role role)
{
    // Elementos largos (objetos 3D, eixos) ocupam a largura inteira
    for (int attempt = 0; attempt < 3; ++attempt) {
        const bool wide = local.width() > columnRect(m_screen, m_column).width() + kEpsilon;
        const QRectF column = wide ? screenArea(m_screen) : columnRect(m_screen, m_column);
        const double top = wide ? std::max(m_pen[0], m_pen[1]) : m_pen[m_column];

        if (top + local.height() <= screenArea(m_screen).bottom() + kEpsilon || attempt == 2) {
            const double x = role == Role::Title ? column.center().x() - local.width() / 2.0 : column.left();
            return QPointF(x, top);
        }
        // Só troca de coluna sozinho quando a aula não escolheu uma; se escolheu,
        // a lista continua na mesma coluna, na tela seguinte
        if (m_twoColumns && !wide && m_column == 0 && !m_columnChosen)
            m_column = 1;
        else
            overflowScreen();
    }
    return screenArea(m_screen).topLeft();
}

bool Layout::nearestFree(const QRectF &area, const QSizeF &size, const QPointF &wanted, QPointF *found) const
{
    if (size.width() > area.width() + kEpsilon || size.height() > area.height() + kEpsilon)
        return false;
    const double step = std::max(0.5, m_params.collisionStep);
    double best = std::numeric_limits<double>::infinity();
    for (double y = area.top(); y <= area.bottom() - size.height() + kEpsilon; y += step) {
        for (double x = area.left(); x <= area.right() - size.width() + kEpsilon; x += step) {
            const double distance = std::hypot(x - wanted.x(), y - wanted.y());
            if (distance >= best)
                continue;
            if (!m_grid.isFree(QRectF(QPointF(x, y), size)))
                continue;
            best = distance;
            *found = QPointF(x, y);
        }
    }
    return std::isfinite(best);
}

void Layout::noteOccupied(const QRectF &box)
{
    const int screen = screenOf(box);
    if (screen > m_screen) {
        // O elemento foi parar numa tela adiante: o fluxo continua de lá
        m_screen = screen;
        m_column = 0;
        m_twoColumns = false;
        m_pen[0] = m_pen[1] = screenArea(screen).top();
    }
    const double bottom = box.bottom() + m_params.flowSpacing;
    for (int column = 0; column < 2; ++column) {
        const QRectF rect = columnRect(m_screen, column);
        if (box.right() > rect.left() - kEpsilon && box.left() < rect.right() + kEpsilon)
            m_pen[column] = std::max(m_pen[column], bottom);
    }
    m_grid.markBox(box);
}

Layout::Placement Layout::place(const QJsonObject &command, const QRectF &local, const QPointF &localAnchor,
                                const std::vector<SceneElement> &elements, Role role)
{
    const QString who = describe(command);
    QPointF offset;
    QPointF direction(0.0, 1.0);           // para onde afastar em caso de colisão
    const SceneElement *allowed = nullptr; // o único elemento que pode ser tocado

    QString side;
    for (const QString &key : kSides)
        if (command.contains(key)) {
            side = key;
            break;
        }
    const bool positioned = command.contains("em") || command.contains("em_centro_de")
                            || command.contains("relativo_a") || command.contains("ancora")
                            || !side.isEmpty();

    // Âncora "centro" quando a referência não existe
    auto fallbackToCenter = [&](const QString &reason) {
        qWarning().noquote() << QString("Layout: %1 — %2; usando \"centro\"").arg(who, reason);
        offset = anchorOffset("centro", local, &direction);
    };

    QPointF at;
    if (!positioned) {
        // O PADRÃO: fluxo tipo documento, o motor decide o "onde"
        offset = flowOrigin(local, role) - local.topLeft();
    } else if (json::readPoint(command.value("em"), &at)) {
        // Coordenada absoluta: hoje é só sugestão (e legado do protocolo)
        offset = at - localAnchor;
    } else if (command.contains("em_centro_de")) {
        const QString id = command.value("em_centro_de").toString();
        if (const SceneElement *ref = find(elements, id)) {
            offset = ref->bounds.center() - local.center();
            allowed = ref; // pode ficar dentro do outro, mas só dele
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
            allowed = ref; // a geometria em volta da referência pode tocá-la
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
        if (!kAnchors.contains(anchor)) {
            qWarning().noquote() << QString("Layout: %1 — âncora \"%2\" desconhecida; usando \"centro\"").arg(who, anchor);
            anchor = "centro";
        }
        offset = anchorOffset(anchor, local, &direction);
    }

    // --- Anticolisão rígida: sobreposição nunca é aceitável ---
    rebuildGrid(elements, allowed);
    QRectF box = local.translated(offset);
    int screen = screenOf(box);
    growTo(screen);
    box = pushInside(box, screenArea(screen));

    if (!m_grid.isFree(box)) {
        const QPointF wanted = box.topLeft();
        bool placed = false;

        // (1) desliza na direção do posicionamento
        const QRectF area = screenArea(screen);
        const double step = std::max(0.5, m_params.collisionStep);
        const double reach = std::hypot(area.width(), area.height());
        for (double t = step; t <= reach && !placed; t += step) {
            const QRectF candidate = pushInside(box.translated(direction * t), area);
            if (m_grid.isFree(candidate)) {
                box = candidate;
                placed = true;
            }
        }

        // (2) o espaço livre mais próximo na tela atual
        QPointF found;
        if (!placed && nearestFree(area, box.size(), wanted, &found)) {
            box.moveTopLeft(found);
            placed = true;
        }

        // (3) uma tela limpa adiante
        for (int next = screen + 1; !placed && next <= screen + m_params.maxScreenGrowth; ++next) {
            growTo(next);
            rebuildGrid(elements, allowed);
            const QRectF nextArea = screenArea(next);
            if (nearestFree(nextArea, box.size(), nextArea.topLeft(), &found)) {
                box.moveTopLeft(found);
                screen = next;
                placed = true;
            }
        }

        // (4) só então, reduzir o elemento
        if (!placed) {
            const QRectF nextArea = screenArea(screen);
            const double scale = std::max(m_params.minScale,
                                          std::min(nextArea.width() / std::max(box.width(), kEpsilon),
                                                   nextArea.height() / std::max(box.height(), kEpsilon)));
            qWarning().noquote() << QString("Layout: %1 — não cabe em nenhuma tela; reduzido para %2%")
                                        .arg(who, QString::number(scale * 100.0, 'f', 0));
            const QRectF scaled(localAnchor + (local.topLeft() - localAnchor) * scale, local.size() * scale);
            QRectF small = pushInside(scaled.translated(offset), nextArea);
            if (nearestFree(nextArea, small.size(), small.topLeft(), &found))
                small.moveTopLeft(found);
            noteOccupied(small);
            // O chamador escala em torno do ponto de referência e depois translada
            return {small.topLeft() - scaled.topLeft(), scale};
        }
    }

    noteOccupied(box);
    return {box.topLeft() - local.topLeft(), 1.0};
}

QPointF Layout::anchorOffset(const QString &anchor, const QRectF &local, QPointF *direction) const
{
    // Horizontal: esquerda / centro / direita; vertical: topo / meio / base
    const QRectF area = screenArea(m_screen);
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

QRectF Layout::pushInside(const QRectF &box, const QRectF &area) const
{
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
