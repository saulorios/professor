#include "Mesh.h"

#include <algorithm>
#include <utility>

int Mesh::addVertex(const QVector3D &p, const QString &name)
{
    vertices.push_back(p);
    vertexNames.push_back(name);
    return int(vertices.size()) - 1;
}

int Mesh::addFace(const QString &name, std::vector<int> vertexIds, const QVector3D &inside)
{
    // Normal pelo método de Newell (robusto para polígonos com muitos vértices)
    QVector3D n;
    const std::size_t count = vertexIds.size();
    for (std::size_t i = 0; i < count; ++i) {
        const QVector3D &a = vertices[vertexIds[i]];
        const QVector3D &b = vertices[vertexIds[(i + 1) % count]];
        n += QVector3D((a.y() - b.y()) * (a.z() + b.z()), (a.z() - b.z()) * (a.x() + b.x()),
                       (a.x() - b.x()) * (a.y() + b.y()));
    }
    MeshFace face;
    face.name = name;
    face.vertices = std::move(vertexIds);
    const QVector3D center = centroid(face);
    if (QVector3D::dotProduct(n, center - inside) < 0.0f) {
        n = -n;
        std::reverse(face.vertices.begin(), face.vertices.end());
    }
    face.normal = n.normalized();
    faces.push_back(face);
    return int(faces.size()) - 1;
}

void Mesh::buildEdges()
{
    edges.clear();
    for (int f = 0; f < int(faces.size()); ++f) {
        const std::vector<int> &ids = faces[f].vertices;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const int a = ids[i], b = ids[(i + 1) % ids.size()];
            const auto shared = std::find_if(edges.begin(), edges.end(), [a, b](const MeshEdge &e) {
                return (e.a == a && e.b == b) || (e.a == b && e.b == a);
            });
            if (shared != edges.end())
                shared->faceB = f;
            else
                edges.push_back({a, b, f, -1});
        }
    }
}

void Mesh::translate(const QVector3D &offset)
{
    for (QVector3D &v : vertices)
        v += offset;
    for (MeshFace &f : faces)
        f.origin += offset;
}

int Mesh::findVertex(const QString &name) const
{
    const auto it = std::find(vertexNames.begin(), vertexNames.end(), name);
    return name.isEmpty() || it == vertexNames.end() ? -1 : int(it - vertexNames.begin());
}

int Mesh::findFace(const QString &name) const
{
    const auto it = std::find_if(faces.begin(), faces.end(), [&](const MeshFace &f) { return f.name == name; });
    return name.isEmpty() || it == faces.end() ? -1 : int(it - faces.begin());
}

QVector3D Mesh::centroid(const MeshFace &face) const
{
    QVector3D sum;
    for (int id : face.vertices)
        sum += vertices[id];
    return face.vertices.empty() ? sum : sum / float(face.vertices.size());
}

void Solid::translate(const QVector3D &offset)
{
    mesh.translate(offset);
    base += offset;
}

bool Solid::namedPoint(const QString &name, QVector3D *out) const
{
    if (kind == Kind::Polyhedron) {
        const int id = mesh.findVertex(name);
        if (id < 0)
            return false;
        *out = mesh.vertices[id];
        return true;
    }

    // Pontos notáveis dos sólidos curvos (esfera: `base` é o centro)
    const QVector3D up(0.0f, 1.0f, 0.0f);
    if (kind == Kind::Sphere) {
        if (name == "centro")
            *out = base;
        else if (name == "topo")
            *out = base + up * radius;
        else if (name == "base")
            *out = base - up * radius;
        else
            return false;
        return true;
    }
    if (name == "base_centro")
        *out = base;
    else if (name == "centro")
        *out = base + up * (height / 2.0f);
    else if (kind == Kind::Cylinder && name == "topo_centro")
        *out = base + up * height;
    else if (kind == Kind::Cone && name == "topo")
        *out = base + up * height;
    else
        return false;
    return true;
}
