#include "HiddenLines.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kFacing = 1e-6f;   // tolerância para "a face olha para o observador"
constexpr float kAhead = 1e-3f;    // o oclusor precisa estar mesmo à frente do ponto
constexpr double kInside = 1e-9;   // ponto exatamente na borda do polígono não conta

// Ponto dentro do polígono (regra do número de cruzamentos)
bool contains(const Polyline &polygon, const QPointF &p)
{
    bool inside = false;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const QPointF &a = polygon[i];
        const QPointF &b = polygon[j];
        if ((a.y() > p.y()) != (b.y() > p.y())) {
            const double x = a.x() + (p.y() - a.y()) / (b.y() - a.y()) * (b.x() - a.x());
            if (p.x() < x - kInside)
                inside = !inside;
        }
    }
    return inside;
}

} // namespace

HiddenLines::HiddenLines(const Projection &projection, const SceneParams &params)
    : m_projection(projection)
    , m_params(params)
{
}

bool HiddenLines::frontFacing(const Mesh &mesh, const MeshFace &face) const
{
    const QVector3D center = mesh.centroid(face);
    return QVector3D::dotProduct(face.normal, m_projection.towardViewer(center)) > kFacing;
}

void HiddenLines::addPart(int part, const Mesh &mesh, const std::vector<bool> *skip)
{
    for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
        const MeshFace &face = mesh.faces[i];
        if ((skip && (*skip)[i]) || !frontFacing(mesh, face))
            continue;
        Occluder occluder;
        occluder.part = part;
        occluder.normal = face.normal;
        occluder.offset = QVector3D::dotProduct(face.normal, mesh.centroid(face));
        for (int id : face.vertices)
            occluder.polygon.push_back(m_projection.project(mesh.vertices[id]));
        m_occluders.push_back(std::move(occluder));
    }
}

bool HiddenLines::occludedByOthers(const QVector3D &p, const std::vector<int> &parts) const
{
    const QPointF projected = m_projection.project(p);
    const QVector3D toward = m_projection.towardViewer(p);
    for (const Occluder &occluder : m_occluders) {
        if (std::find(parts.begin(), parts.end(), occluder.part) != parts.end())
            continue;
        const float denominator = QVector3D::dotProduct(occluder.normal, toward);
        if (std::abs(denominator) < kFacing)
            continue;
        // Distância, ao longo do raio até o observador, do ponto ao plano da face
        const float t = (occluder.offset - QVector3D::dotProduct(occluder.normal, p)) / denominator;
        if (t > kAhead && contains(occluder.polygon, projected))
            return true;
    }
    return false;
}

std::vector<HiddenLines::Piece> HiddenLines::split(const std::vector<QVector3D> &path,
                                                   const std::vector<int> &parts, bool selfVisible) const
{
    std::vector<Piece> pieces;
    if (path.size() < 2)
        return pieces;

    const auto push = [&pieces](const QPointF &point, bool visible) {
        if (pieces.empty() || pieces.back().visible != visible) {
            // O trecho novo começa onde o anterior terminou (sem buraco)
            Piece piece;
            piece.visible = visible;
            if (!pieces.empty())
                piece.points.push_back(pieces.back().points.back());
            pieces.push_back(std::move(piece));
        }
        pieces.back().points.push_back(point);
    };

    QPointF previous = m_projection.project(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) {
        const QVector3D a = path[i - 1];
        const QVector3D b = path[i];
        const int steps = std::max(1, int(std::ceil(double((b - a).length()) / m_params.hiddenSampleSpacing)));
        for (int s = 1; s <= steps; ++s) {
            const float t = float(s) / float(steps);
            const QVector3D end = a + (b - a) * t;
            const QVector3D middle = a + (b - a) * (float(s) - 0.5f) / float(steps);
            const bool visible = selfVisible && !occludedByOthers(middle, parts);
            if (pieces.empty()) {
                Piece piece;
                piece.visible = visible;
                piece.points.push_back(previous);
                pieces.push_back(std::move(piece));
            }
            push(m_projection.project(end), visible);
        }
    }

    // Trechos degenerados (um ponto só) não interessam
    pieces.erase(std::remove_if(pieces.begin(), pieces.end(),
                                [](const Piece &piece) { return piece.points.size() < 2; }),
                 pieces.end());
    return pieces;
}
