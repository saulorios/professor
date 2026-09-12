#include "Object3D.h"
#include "HiddenLines.h"
#include "JsonHelpers.h"
#include "solids/Solids.h"

#include <QDebug>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kJoinTolerance = 1e-6;  // pontas de traços que se encontram
constexpr float kCollinear = 1e-4f;
constexpr float kAxis = 0.999f;          // cosseno mínimo para a aresta seguir um eixo

// Ordem de desenho, como um professor faria
enum Category { CategoryFront = 0, CategoryDepth, CategoryOther, CategoryHidden, CategoryDecal, CategoryCount };

// Trecho já projetado (ainda sem escala nem posição)
struct Piece {
    int category = CategoryOther;
    bool dashed = false;
    Polyline points;
};

// Aresta reta em 3D, antes da projeção
struct Segment {
    QVector3D a;
    QVector3D b;
    int category = CategoryOther;
    int axis = -1;
    bool selfVisible = true;
    std::vector<int> parts;
};

double length(const QPointF &p)
{
    return std::hypot(p.x(), p.y());
}

int axisOf(const QVector3D &direction)
{
    const QVector3D d = direction.normalized();
    if (std::abs(d.x()) > kAxis)
        return 0;
    if (std::abs(d.y()) > kAxis)
        return 1;
    if (std::abs(d.z()) > kAxis)
        return 2;
    return -1;
}

// Funde arestas colineares que se tocam (ex.: base do telhado sobre o topo da
// caixa), para que a mão desenhe cada linha uma vez só
void mergeSegments(std::vector<Segment> &segments)
{
    for (std::size_t i = 0; i < segments.size(); ++i) {
        for (std::size_t j = i + 1; j < segments.size();) {
            Segment &s = segments[i];
            const Segment &t = segments[j];
            const QVector3D ds = s.b - s.a;
            const float len = ds.length();
            const QVector3D unit = len > 0.0f ? ds / len : ds;
            const bool sameLine = QVector3D::crossProduct(unit, (t.b - t.a).normalized()).length() < kCollinear
                                  && QVector3D::crossProduct(unit, t.a - s.a).length() < kCollinear;
            if (!sameLine) {
                ++j;
                continue;
            }
            // Parâmetros ao longo da reta: só funde se os intervalos se tocam
            const float ta = QVector3D::dotProduct(t.a - s.a, unit);
            const float tb = QVector3D::dotProduct(t.b - s.a, unit);
            const float low = std::min(ta, tb), high = std::max(ta, tb);
            if (high < -kCollinear || low > len + kCollinear) {
                ++j;
                continue;
            }
            const float from = std::min(0.0f, low), to = std::max(len, high);
            s.a = s.a + unit * from;
            s.b = s.a + unit * (to - from);
            s.category = std::min(s.category, t.category);
            s.selfVisible = s.selfVisible || t.selfVisible;
            for (int part : t.parts)
                if (std::find(s.parts.begin(), s.parts.end(), part) == s.parts.end())
                    s.parts.push_back(part);
            segments.erase(segments.begin() + std::ptrdiff_t(j));
            j = i + 1; // a fusão pode ter aberto caminho para outras
        }
    }
}

// Soluções de A·cos θ + B·sen θ = c (ângulos da silhueta dos sólidos curvos)
std::vector<double> silhouetteAngles(double a, double b, double c)
{
    const double radius = std::hypot(a, b);
    if (radius < 1e-9 || std::abs(c) > radius)
        return {};
    const double base = std::atan2(b, a);
    const double delta = std::acos(std::clamp(c / radius, -1.0, 1.0));
    return {base - delta, base + delta};
}

// Liga trechos cujas pontas se encontram, para a mão desenhar de uma vez só
std::vector<Polyline> chain(std::vector<Polyline> lines)
{
    std::vector<Polyline> chains;
    std::vector<bool> used(lines.size(), false);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (used[i])
            continue;
        used[i] = true;
        Polyline current = lines[i];
        bool grew = true;
        while (grew) {
            grew = false;
            for (std::size_t j = 0; j < lines.size(); ++j) {
                if (used[j] || lines[j].size() < 2)
                    continue;
                if (length(lines[j].front() - current.back()) < kJoinTolerance)
                    current.insert(current.end(), lines[j].begin() + 1, lines[j].end());
                else if (length(lines[j].back() - current.back()) < kJoinTolerance)
                    current.insert(current.end(), lines[j].rbegin() + 1, lines[j].rend());
                else if (length(lines[j].back() - current.front()) < kJoinTolerance)
                    current.insert(current.begin(), lines[j].begin(), lines[j].end() - 1);
                else if (length(lines[j].front() - current.front()) < kJoinTolerance)
                    current.insert(current.begin(), lines[j].rbegin(), lines[j].rend() - 1);
                else
                    continue;
                used[j] = true;
                grew = true;
            }
        }
        chains.push_back(std::move(current));
    }
    // De cima para baixo, da esquerda para a direita
    std::stable_sort(chains.begin(), chains.end(), [](const Polyline &p, const Polyline &q) {
        if (std::abs(p.front().y() - q.front().y()) > 0.5)
            return p.front().y() < q.front().y();
        return p.front().x() < q.front().x();
    });
    return chains;
}

// Fecho convexo (cadeia monótona de Andrew) da silhueta projetada
Polyline convexHull(std::vector<QPointF> points)
{
    if (points.size() < 3)
        return Polyline(points.begin(), points.end());
    std::sort(points.begin(), points.end(), [](const QPointF &p, const QPointF &q) {
        return p.x() != q.x() ? p.x() < q.x() : p.y() < q.y();
    });
    const auto cross = [](const QPointF &o, const QPointF &a, const QPointF &b) {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    Polyline hull(2 * points.size());
    std::size_t k = 0;
    for (const QPointF &p : points) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], p) <= 0)
            --k;
        hull[k++] = p;
    }
    const std::size_t lower = k + 1;
    for (std::size_t i = points.size() - 1; i-- > 0;) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], points[i]) <= 0)
            --k;
        hull[k++] = points[i];
    }
    hull.resize(k);
    return hull;
}

} // namespace

Object3DBuilder::Object3DBuilder(const SceneParams &params, const Layout &layout, const TextLayout &text)
    : m_params(params)
    , m_geometry(params)
    , m_layout(layout)
    , m_text(text)
{
}

bool Object3DBuilder::build(const QJsonObject &command, const std::vector<SceneElement> &elements,
                            PressureLevel pressure, std::vector<ObjectStroke> *strokes,
                            Object3DInfo *info, QString *error) const
{
    // --- Vista ---
    const QString viewName = command.value("vista").toString("cavaleira");
    Projection::View view = Projection::View::Cavalier;
    if (viewName == "isometrica")
        view = Projection::View::Isometric;
    else if (viewName == "perspectiva_1")
        view = Projection::View::OnePoint;
    else if (viewName == "perspectiva_2")
        view = Projection::View::TwoPoint;
    else if (viewName != "cavaleira") {
        *error = QString("vista \"%1\" desconhecida (cavaleira, isometrica, perspectiva_1, perspectiva_2)").arg(viewName);
        return false;
    }

    double rotation = m_params.twoPointRotation;
    if (command.contains("rotacao") && !json::readNumber(command, "rotacao", &rotation))
        qWarning().noquote() << "objeto_3d: \"rotacao\" inválida; usando" << m_params.twoPointRotation;
    const QString eyeName = command.value("altura_olho").toString("media");
    double eyeFactor = m_params.eyeMedium;
    if (eyeName == "baixa")
        eyeFactor = m_params.eyeLow;
    else if (eyeName == "alta")
        eyeFactor = m_params.eyeHigh;
    else if (eyeName != "media")
        qWarning().noquote() << "objeto_3d: altura_olho" << eyeName << "desconhecida - usando media";

    // --- Partes ---
    const QJsonArray parts = command.value("partes").toArray();
    if (parts.isEmpty()) {
        *error = "objeto_3d precisa de \"partes\"";
        return false;
    }
    std::vector<Solid> solids;
    for (const QJsonValue &value : parts) {
        const QJsonObject part = value.toObject();
        const QString kind = part.value("solido").toString();
        const QJsonArray size = part.value("tamanho").toArray();
        const auto number = [&part](const char *key, double fallback) {
            double out = fallback;
            json::readNumber(part, key, &out);
            return out;
        };
        Solid solid;
        if (kind == "caixa" || kind == "prisma_triangular") {
            if (size.size() != 3) {
                *error = QString("%1 precisa de \"tamanho\" [largura, altura, profundidade]").arg(kind);
                return false;
            }
            const auto w = float(size[0].toDouble()), h = float(size[1].toDouble()), d = float(size[2].toDouble());
            if (w <= 0.0f || h <= 0.0f || d <= 0.0f) {
                *error = QString("%1 com \"tamanho\" não positivo").arg(kind);
                return false;
            }
            solid = kind == "caixa" ? solids::box(w, h, d) : solids::triangularPrism(w, h, d);
        } else if (kind == "piramide") {
            const QJsonArray base = part.value("base").toArray();
            const auto height = float(number("altura", 0.0));
            const int sides = part.value("lados").toInt(4);
            if (base.size() != 2 || base[0].toDouble() <= 0 || base[1].toDouble() <= 0 || height <= 0.0f) {
                *error = "piramide precisa de \"base\" [largura, profundidade] e \"altura\" > 0";
                return false;
            }
            if (sides < 3 || sides > 8)
                qWarning().noquote() << "objeto_3d: piramide com" << sides << "lados - ajustado para 3..8";
            solid = solids::pyramid(float(base[0].toDouble()), float(base[1].toDouble()), height, sides);
        } else if (kind == "cilindro" || kind == "cone" || kind == "esfera") {
            const auto radius = float(number("raio", 0.0));
            const auto height = float(number("altura", 0.0));
            if (radius <= 0.0f || (kind != "esfera" && height <= 0.0f)) {
                *error = QString("%1 precisa de \"raio\"%2 > 0").arg(kind, kind == "esfera" ? "" : " e \"altura\"");
                return false;
            }
            solid = kind == "cilindro" ? solids::cylinder(radius, height, m_params.curveSegments)
                  : kind == "cone"     ? solids::cone(radius, height, m_params.curveSegments)
                                       : solids::sphere(radius, m_params.curveSegments, m_params.sphereRings);
        } else {
            *error = QString("sólido \"%1\" não suportado").arg(kind);
            return false;
        }

        QVector3D position;
        const QJsonArray at = part.value("posicao").toArray();
        if (at.size() == 3)
            position = QVector3D(float(at[0].toDouble()), float(at[1].toDouble()), float(at[2].toDouble()));
        else if (!at.isEmpty())
            qWarning().noquote() << "objeto_3d: \"posicao\" precisa de [x, y, z]; usando a origem";
        solid.translate(position);
        solid.id = part.value("id").toString(QString("parte_%1").arg(solids.size() + 1));
        solids.push_back(std::move(solid));
    }

    // --- Projeção e oclusão ---
    std::vector<QVector3D> allPoints;
    for (const Solid &solid : solids)
        allPoints.insert(allPoints.end(), solid.mesh.vertices.begin(), solid.mesh.vertices.end());
    const Projection projection(view, m_params, allPoints, rotation, eyeFactor);

    // Faces coladas entre duas partes (ex.: a base do telhado sobre o topo da
    // caixa) são internas: não escondem nada e não tornam aresta nenhuma visível
    std::vector<std::vector<bool>> internal(solids.size());
    for (std::size_t i = 0; i < solids.size(); ++i)
        internal[i].assign(solids[i].mesh.faces.size(), false);
    for (std::size_t i = 0; i < solids.size(); ++i)
        for (std::size_t j = i + 1; j < solids.size(); ++j)
            for (std::size_t fi = 0; fi < solids[i].mesh.faces.size(); ++fi)
                for (std::size_t fj = 0; fj < solids[j].mesh.faces.size(); ++fj) {
                    const MeshFace &a = solids[i].mesh.faces[fi];
                    const MeshFace &b = solids[j].mesh.faces[fj];
                    if (QVector3D::dotProduct(a.normal, b.normal) > -0.999f)
                        continue;
                    if ((solids[i].mesh.centroid(a) - solids[j].mesh.centroid(b)).length() > 1e-3f)
                        continue;
                    internal[i][fi] = internal[j][fj] = true;
                }

    HiddenLines hidden(projection, m_params);
    for (int i = 0; i < int(solids.size()); ++i)
        hidden.addPart(i, solids[i].mesh, &internal[std::size_t(i)]);

    const QString hiddenMode = command.value("arestas_ocultas")
                                   .toString(projection.isPerspective() ? "omitir" : "tracejar");
    if (hiddenMode != "omitir" && hiddenMode != "tracejar")
        qWarning().noquote() << "objeto_3d: arestas_ocultas" << hiddenMode << "desconhecido - usando tracejar";
    const bool drawHidden = hiddenMode != "omitir";

    std::vector<Piece> pieces;
    std::vector<Polyline> hiddenLines;
    std::vector<Object3DEdge> edges;

    // Recolhe um trecho já dividido em visível/oculto
    const auto addPath = [&](const std::vector<QVector3D> &path, const std::vector<int> &owners, bool selfVisible,
                          int category, bool dashed) {
        bool anyVisible = false;
        for (const HiddenLines::Piece &piece : hidden.split(path, owners, selfVisible)) {
            if (piece.visible) {
                anyVisible = true;
                pieces.push_back({category, dashed, piece.points});
            } else {
                hiddenLines.push_back(piece.points);
                if (drawHidden && category != CategoryDecal)
                    pieces.push_back({CategoryHidden, true, piece.points});
            }
        }
        return anyVisible;
    };

    // --- Arestas dos poliedros ---
    std::vector<Segment> segments;
    for (int i = 0; i < int(solids.size()); ++i) {
        const Solid &solid = solids[i];
        if (solid.kind != Solid::Kind::Polyhedron)
            continue;
        for (const MeshEdge &edge : solid.mesh.edges) {
            Segment segment;
            segment.a = solid.mesh.vertices[edge.a];
            segment.b = solid.mesh.vertices[edge.b];
            segment.axis = axisOf(segment.b - segment.a);
            segment.parts = {i};
            bool front = false;
            segment.selfVisible = false;
            for (int face : {edge.faceA, edge.faceB}) {
                if (face < 0 || internal[std::size_t(i)][std::size_t(face)]
                    || !hidden.frontFacing(solid.mesh, solid.mesh.faces[face]))
                    continue;
                segment.selfVisible = true;
                front = front || solid.mesh.faces[face].name == "frente";
            }
            segment.category = front ? CategoryFront : segment.axis == 2 ? CategoryDepth : CategoryOther;
            segments.push_back(std::move(segment));
        }
    }
    mergeSegments(segments);

    for (const Segment &segment : segments) {
        const bool visible = addPath({segment.a, segment.b}, segment.parts, segment.selfVisible,
                                  segment.category, false);
        edges.push_back({projection.project(segment.a), projection.project(segment.b),
                         double((segment.b - segment.a).length()), segment.axis, visible});
    }

    // --- Sólidos curvos: elipses das bases e geratrizes de contorno ---
    const auto circle3D = [this](const QVector3D &center, const QVector3D &u, const QVector3D &v,
                                 double from, double to) {
        std::vector<QVector3D> path;
        const int steps = std::max(2, int(std::ceil(std::abs(to - from) / (2.0 * kPi) * m_params.curveSamples)));
        for (int i = 0; i <= steps; ++i) {
            const double angle = from + (to - from) * i / steps;
            path.push_back(center + u * float(std::cos(angle)) + v * float(std::sin(angle)));
        }
        return path;
    };

    for (int i = 0; i < int(solids.size()); ++i) {
        const Solid &solid = solids[i];
        if (solid.kind == Solid::Kind::Polyhedron)
            continue;
        const std::vector<int> owners{i};
        const QVector3D up(0.0f, 1.0f, 0.0f);
        const float r = solid.radius;

        if (solid.kind == Solid::Kind::Sphere) {
            // Contorno: círculo máximo (paralela) ou pequeno círculo (perspectiva)
            const QVector3D center = solid.base;
            QVector3D normal = projection.towardViewer(center);
            QVector3D silhouetteCenter = center;
            float silhouetteRadius = r;
            if (projection.isPerspective()) {
                const QVector3D toEye = projection.eye() - center;
                const float d = toEye.length();
                if (d <= r * 1.001f) {
                    *error = "esfera: o observador está dentro dela";
                    return false;
                }
                normal = toEye / d;
                silhouetteCenter = center + normal * (r * r / d);
                silhouetteRadius = r * std::sqrt(1.0f - r * r / (d * d));
            }
            QVector3D u = QVector3D::crossProduct(normal, up);
            if (u.length() < 1e-4f)
                u = QVector3D::crossProduct(normal, QVector3D(1.0f, 0.0f, 0.0f));
            u = u.normalized() * silhouetteRadius;
            const QVector3D v = QVector3D::crossProduct(normal, u).normalized() * silhouetteRadius;
            addPath(circle3D(silhouetteCenter, u, v, 0.0, 2.0 * kPi), owners, true, CategoryOther, false);
            // Equador tracejado, como guia do desenho
            addPath(circle3D(center, QVector3D(r, 0, 0), QVector3D(0, 0, r), 0.0, 2.0 * kPi), owners, true,
                 CategoryOther, true);
            continue;
        }

        const bool isCone = solid.kind == Solid::Kind::Cone;
        const float h = solid.height;
        const QVector3D base = solid.base;
        const QVector3D apex = base + up * h;

        // Ângulos da silhueta lateral
        std::vector<double> angles;
        if (projection.isPerspective()) {
            const QVector3D eye = projection.eye();
            angles = isCone ? silhouetteAngles(eye.x() - base.x(), eye.z() - base.z(),
                                               double(r) * (1.0 - double(eye.y() - base.y()) / double(h)))
                            : silhouetteAngles(eye.x() - base.x(), eye.z() - base.z(), r);
        } else {
            const QVector3D t = projection.towardViewer(base);
            angles = isCone ? silhouetteAngles(double(h) * t.x(), double(h) * t.z(), -double(r) * t.y())
                            : silhouetteAngles(t.x(), t.z(), 0.0);
        }

        // A lateral olha para o observador neste ângulo?
        const auto sideFaces = [&](double angle) {
            const auto c = float(std::cos(angle)), s = float(std::sin(angle));
            const QVector3D point = base + QVector3D(r * c, isCone ? h / 4.0f : h / 2.0f, r * s);
            const QVector3D normal = isCone ? QVector3D(h * c, r, h * s).normalized() : QVector3D(c, 0.0f, s);
            return QVector3D::dotProduct(normal, projection.towardViewer(point)) > 0.0f;
        };
        const auto capFaces = [&](const QVector3D &center, const QVector3D &normal) {
            return QVector3D::dotProduct(normal, projection.towardViewer(center)) > 0.0f;
        };

        // Geratrizes de contorno (tangentes às elipses das bases)
        for (double angle : angles) {
            const QVector3D rim = base + QVector3D(r * float(std::cos(angle)), 0.0f, r * float(std::sin(angle)));
            addPath({rim, isCone ? apex : rim + up * h}, owners, true, CategoryOther, false);
        }

        // Elipses das bases, arco a arco: a metade de trás só aparece se a tampa estiver visível
        const bool baseVisible = capFaces(base, -up);
        const bool topVisible = capFaces(apex, up);
        std::vector<std::pair<double, double>> arcs;
        if (angles.size() == 2)
            arcs = {{angles[0], angles[1]}, {angles[1], angles[0] + 2.0 * kPi}};
        else
            arcs = {{0.0, 2.0 * kPi}};
        for (const auto &arc : arcs) {
            const bool side = sideFaces((arc.first + arc.second) / 2.0);
            const QVector3D u(r, 0, 0), v(0, 0, r);
            addPath(circle3D(base, u, v, arc.first, arc.second), owners, side || baseVisible, CategoryOther, false);
            if (!isCone)
                addPath(circle3D(base + up * h, u, v, arc.first, arc.second), owners, side || topVisible,
                     CategoryOther, false);
        }
    }

    // --- Decalques ---
    for (const QJsonValue &value : command.value("decalques").toArray()) {
        const QJsonObject decal = value.toObject();
        const QString partId = decal.value("parte").toString();
        const auto part = std::find_if(solids.begin(), solids.end(),
                                       [&](const Solid &s) { return s.id == partId; });
        if (part == solids.end()) {
            qWarning().noquote() << "objeto_3d: decalque em parte inexistente:" << partId;
            continue;
        }
        const int index = int(part - solids.begin());
        const QString faceName = decal.value("face").toString();
        const int faceId = part->mesh.findFace(faceName);
        if (faceId < 0) {
            qWarning().noquote() << "objeto_3d: decalque em face inexistente:" << faceName;
            continue;
        }
        const MeshFace &face = part->mesh.faces[faceId];
        if (internal[std::size_t(index)][std::size_t(faceId)] || !hidden.frontFacing(part->mesh, face))
            continue; // face escondida: o decalque não é desenhado

        const auto number = [&decal](const char *key, double fallback) {
            double out = fallback;
            json::readNumber(decal, key, &out);
            return out;
        };
        const double u = number("u", 0.0), v = number("v", 0.0);
        const double width = number("largura", 0.2), height = number("altura", 0.2);
        // (u, v) → ponto da face em 3D; v = 0 é a base da face
        const auto onFace = [&face](double fu, double fv) {
            return face.origin + face.uAxis * float(fu) + face.vAxis * float(fv);
        };

        const QString shape = decal.value("forma").toString("retangulo");
        std::vector<std::vector<QVector3D>> paths;
        if (shape == "retangulo") {
            paths.push_back({onFace(u, v), onFace(u + width, v), onFace(u + width, v + height),
                             onFace(u, v + height), onFace(u, v)});
        } else if (shape == "linha") {
            paths.push_back({onFace(u, v), onFace(u + width, v + height)});
        } else if (shape == "circulo") {
            std::vector<QVector3D> path;
            for (int i = 0; i <= m_params.curveSamples; ++i) {
                const double angle = 2.0 * kPi * i / m_params.curveSamples;
                path.push_back(onFace(u + width / 2 * (1 + std::cos(angle)),
                                      v + height / 2 * (1 + std::sin(angle))));
            }
            paths.push_back(std::move(path));
        } else if (shape == "texto") {
            const std::vector<Polyline> text = m_text.layout(decal.value("texto").toString(), 1.0);
            const QRectF box = Geometry2D::bounds(text);
            if (box.isEmpty())
                continue;
            for (const Polyline &line : text) {
                std::vector<QVector3D> path;
                for (const QPointF &p : line) // o texto tem Y para baixo; v da face sobe
                    path.push_back(onFace(u + width * (p.x() - box.left()) / box.width(),
                                          v + height * (box.bottom() - p.y()) / box.height()));
                paths.push_back(std::move(path));
            }
        } else {
            qWarning().noquote() << "objeto_3d: forma de decalque desconhecida:" << shape;
            continue;
        }
        for (const std::vector<QVector3D> &path : paths)
            addPath(path, {index}, true, CategoryDecal, false);
    }

    if (pieces.empty()) {
        *error = "objeto_3d: nada visível para desenhar";
        return false;
    }

    // --- Escala ---
    double scale = 1.0;
    if (projection.isPerspective()) {
        // A aresta vertical mais próxima do observador fica com a altura declarada
        double best = -1.0, bestLength = 0.0, bestProjected = 0.0;
        for (const Segment &segment : segments) {
            if (segment.axis != 1)
                continue;
            const QVector3D middle = (segment.a + segment.b) / 2.0f;
            const double distance = double((projection.eye() - middle).length());
            const double projected = length(projection.project(segment.b) - projection.project(segment.a));
            if (projected > 1e-9 && (best < 0.0 || distance < best)) {
                best = distance;
                bestLength = double((segment.b - segment.a).length());
                bestProjected = projected;
            }
        }
        if (best >= 0.0) {
            scale = bestLength / bestProjected;
        } else {
            // Sem arestas verticais (sólidos curvos): usa a altura do objeto
            double low = allPoints.front().y(), high = low, top = 1e9, bottom = -1e9;
            for (const QVector3D &p : allPoints) {
                low = std::min(low, double(p.y()));
                high = std::max(high, double(p.y()));
                const QPointF q = projection.project(p);
                top = std::min(top, q.y());
                bottom = std::max(bottom, q.y());
            }
            if (bottom > top)
                scale = (high - low) / (bottom - top);
        }
    }

    // --- Caber na área útil ---
    std::vector<QPointF> all;
    for (const Piece &piece : pieces)
        all.insert(all.end(), piece.points.begin(), piece.points.end());
    const QRectF raw = Geometry2D::bounds({Polyline(all.begin(), all.end())});
    const QRectF area = m_layout.usableArea();
    const double fit = std::min(area.width() / std::max(raw.width() * scale, 1e-6),
                                area.height() / std::max(raw.height() * scale, 1e-6));
    if (fit < 1.0) {
        qWarning().noquote() << QString("objeto_3d: \"%1\" não cabe na área útil; escala reduzida para %2%")
                                    .arg(command.value("id").toString(), QString::number(fit * 100.0, 'f', 0));
        scale *= fit;
    }

    const QRectF local(raw.topLeft() * scale, raw.size() * scale);
    const QPointF offset = m_layout.place(command, local, local.center(), elements);
    const auto map = [scale, offset](const QPointF &p) { return p * scale + offset; };

    // --- Traços, na ordem de desenho ---
    strokes->clear();
    for (int category = 0; category < CategoryCount; ++category) {
        for (bool dashed : {false, true}) {
            std::vector<Polyline> group;
            for (const Piece &piece : pieces)
                if (piece.category == category && piece.dashed == dashed)
                    group.push_back(piece.points);
            if (group.empty())
                continue;
            for (Polyline &line : chain(std::move(group))) {
                for (QPointF &p : line)
                    p = map(p);
                if (!dashed) {
                    strokes->push_back({line, pressure});
                    continue;
                }
                // Tracejado com pressão leve, como as linhas auxiliares
                for (Polyline &dash : m_geometry.styled({line}, LineStyle::Dashed))
                    strokes->push_back({dash, PressureLevel::Light});
            }
        }
    }

    // --- Informações para a cena (cotas, rótulos e depuração) ---
    info->id = command.value("id").toString();
    info->bounds = QRectF(map(raw.topLeft()), map(raw.bottomRight()));
    info->edges.clear();
    for (Object3DEdge edge : edges) {
        edge.a = map(edge.a);
        edge.b = map(edge.b);
        info->edges.push_back(edge);
    }
    info->vertices.clear();
    for (const Solid &solid : solids)
        for (std::size_t i = 0; i < solid.mesh.vertices.size(); ++i)
            if (!solid.mesh.vertexNames[i].isEmpty())
                info->vertices.push_back({solid.id, solid.mesh.vertexNames[i],
                                          map(projection.project(solid.mesh.vertices[i]))});
    info->hiddenLines.clear();
    for (Polyline line : hiddenLines) {
        for (QPointF &p : line)
            p = map(p);
        info->hiddenLines.push_back(line);
    }

    std::vector<QPointF> visiblePoints;
    for (const Piece &piece : pieces)
        for (const QPointF &p : piece.points)
            visiblePoints.push_back(map(p));
    info->hull = convexHull(visiblePoints);

    // Pontos de fuga e linhas finas até eles (só na depuração)
    info->vanishingPoints.clear();
    info->vanishingLines.clear();
    const QVector3D directions[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int axis = 0; axis < 3; ++axis) {
        QPointF point;
        if (!projection.vanishingPoint(directions[axis], &point))
            continue;
        point = map(point);
        if (length(point - info->bounds.center()) > m_params.vanishingLimit)
            continue;
        info->vanishingPoints.push_back(point);
        for (const Object3DEdge &edge : info->edges) {
            if (edge.axis != axis)
                continue;
            const QPointF farthest = length(edge.a - point) > length(edge.b - point) ? edge.a : edge.b;
            info->vanishingLines.push_back({point, farthest});
        }
    }
    return true;
}
