#include "Solids.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979f;

// Referencial dos decalques de uma face: P(u, v) = origin + u·uAxis + v·vAxis
void setFrame(Mesh &mesh, int face, const QVector3D &origin, const QVector3D &uAxis, const QVector3D &vAxis)
{
    mesh.faces[face].origin = origin;
    mesh.faces[face].uAxis = uAxis;
    mesh.faces[face].vAxis = vAxis;
}

// Anel horizontal de `segments` vértices em torno do eixo Y, na altura y
std::vector<int> ring(Mesh &mesh, float radius, float y, int segments)
{
    std::vector<int> ids;
    for (int i = 0; i < segments; ++i) {
        const float a = 2.0f * kPi * float(i) / float(segments);
        ids.push_back(mesh.addVertex(QVector3D(radius * std::cos(a), y, radius * std::sin(a))));
    }
    return ids;
}

// Tampa circular (base ou topo) com referencial no quadrado que a contém
void cap(Mesh &mesh, const QString &name, const std::vector<int> &ids, float radius, float y, const QVector3D &inside)
{
    const int face = mesh.addFace(name, ids, inside);
    setFrame(mesh, face, QVector3D(-radius, y, -radius), QVector3D(2.0f * radius, 0.0f, 0.0f),
             QVector3D(0.0f, 0.0f, 2.0f * radius));
}

} // namespace

namespace solids {

Solid box(float width, float height, float depth)
{
    const float a = width / 2.0f, c = depth / 2.0f;
    Solid solid;
    Mesh &m = solid.mesh;
    const int fbe = m.addVertex({-a, 0, -c}, "frente_base_esquerda");
    const int fbd = m.addVertex({a, 0, -c}, "frente_base_direita");
    const int ftd = m.addVertex({a, height, -c}, "frente_topo_direita");
    const int fte = m.addVertex({-a, height, -c}, "frente_topo_esquerda");
    const int tbe = m.addVertex({-a, 0, c}, "tras_base_esquerda");
    const int tbd = m.addVertex({a, 0, c}, "tras_base_direita");
    const int ttd = m.addVertex({a, height, c}, "tras_topo_direita");
    const int tte = m.addVertex({-a, height, c}, "tras_topo_esquerda");
    const QVector3D inside(0.0f, height / 2.0f, 0.0f);
    const std::vector<QVector3D> &v = m.vertices;

    // Cada face com u da esquerda para a direita (vista de fora) e v = 0 na base
    int f = m.addFace("frente", {fbe, fbd, ftd, fte}, inside);
    setFrame(m, f, v[fbe], v[fbd] - v[fbe], v[fte] - v[fbe]);
    f = m.addFace("tras", {tbd, tbe, tte, ttd}, inside);
    setFrame(m, f, v[tbd], v[tbe] - v[tbd], v[ttd] - v[tbd]);
    f = m.addFace("esquerda", {tbe, fbe, fte, tte}, inside);
    setFrame(m, f, v[tbe], v[fbe] - v[tbe], v[tte] - v[tbe]);
    f = m.addFace("direita", {fbd, tbd, ttd, ftd}, inside);
    setFrame(m, f, v[fbd], v[tbd] - v[fbd], v[ftd] - v[fbd]);
    f = m.addFace("topo", {fte, ftd, ttd, tte}, inside);
    setFrame(m, f, v[fte], v[ftd] - v[fte], v[tte] - v[fte]);
    f = m.addFace("base", {fbe, fbd, tbd, tbe}, inside);
    setFrame(m, f, v[fbe], v[fbd] - v[fbe], v[tbe] - v[fbe]);
    m.buildEdges();
    return solid;
}

Solid triangularPrism(float width, float height, float depth)
{
    const float a = width / 2.0f, c = depth / 2.0f;
    Solid solid;
    Mesh &m = solid.mesh;
    const int fbe = m.addVertex({-a, 0, -c}, "frente_base_esquerda");
    const int fbd = m.addVertex({a, 0, -c}, "frente_base_direita");
    const int ft = m.addVertex({0, height, -c}, "frente_topo");
    const int tbe = m.addVertex({-a, 0, c}, "tras_base_esquerda");
    const int tbd = m.addVertex({a, 0, c}, "tras_base_direita");
    const int tt = m.addVertex({0, height, c}, "tras_topo");
    const QVector3D inside(0.0f, height / 3.0f, 0.0f);
    const std::vector<QVector3D> &v = m.vertices;
    const QVector3D up(0.0f, height, 0.0f);

    // Triângulos: referencial no retângulo que os contém
    int f = m.addFace("frente", {fbe, fbd, ft}, inside);
    setFrame(m, f, v[fbe], v[fbd] - v[fbe], up);
    f = m.addFace("tras", {tbd, tbe, tt}, inside);
    setFrame(m, f, v[tbd], v[tbe] - v[tbd], up);
    // Águas inclinadas: v sobe da beirada até a cumeeira
    f = m.addFace("esquerda", {tbe, fbe, ft, tt}, inside);
    setFrame(m, f, v[tbe], v[fbe] - v[tbe], v[ft] - v[fbe]);
    f = m.addFace("direita", {fbd, tbd, tt, ft}, inside);
    setFrame(m, f, v[fbd], v[tbd] - v[fbd], v[ft] - v[fbd]);
    f = m.addFace("base", {fbe, fbd, tbd, tbe}, inside);
    setFrame(m, f, v[fbe], v[fbd] - v[fbe], v[tbe] - v[fbe]);
    m.buildEdges();
    return solid;
}

Solid pyramid(float width, float depth, float height, int sides)
{
    sides = std::clamp(sides, 3, 8);
    // Polígono regular com uma aresta de frente (centrada em -90°, lado -z),
    // depois esticado para ocupar exatamente width × depth
    std::vector<QVector3D> base;
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (int k = 0; k < sides; ++k) {
        const float phi = -kPi / 2.0f - kPi / float(sides) + 2.0f * kPi * float(k) / float(sides);
        base.emplace_back(std::cos(phi), 0.0f, std::sin(phi));
        minX = std::min(minX, base.back().x());
        maxX = std::max(maxX, base.back().x());
        minZ = std::min(minZ, base.back().z());
        maxZ = std::max(maxZ, base.back().z());
    }
    QVector3D center;
    for (QVector3D &p : base) {
        p.setX(-width / 2.0f + (p.x() - minX) / (maxX - minX) * width);
        p.setZ(-depth / 2.0f + (p.z() - minZ) / (maxZ - minZ) * depth);
        center += p;
    }
    center /= float(sides);

    // Quatro lados: mesmos nomes da caixa; senão base_1..base_n e lateral_1..
    const bool square = sides == 4;
    const char *squareNames[] = {"frente_base_esquerda", "frente_base_direita", "tras_base_direita", "tras_base_esquerda"};
    const char *squareFaces[] = {"frente", "direita", "tras", "esquerda"};

    Solid solid;
    Mesh &m = solid.mesh;
    std::vector<int> ids;
    for (int k = 0; k < sides; ++k)
        ids.push_back(m.addVertex(base[k], square ? QString(squareNames[k]) : QString("base_%1").arg(k + 1)));
    const int apex = m.addVertex(center + QVector3D(0.0f, height, 0.0f), "topo");
    const QVector3D inside = center + QVector3D(0.0f, height / 4.0f, 0.0f);
    const std::vector<QVector3D> &v = m.vertices;

    for (int k = 0; k < sides; ++k) {
        const int a = ids[k], b = ids[(k + 1) % sides];
        const QString name = square ? QString(squareFaces[k]) : k == 0 ? QString("frente") : QString("lateral_%1").arg(k);
        const int f = m.addFace(name, {a, b, apex}, inside);
        setFrame(m, f, v[a], v[b] - v[a], v[apex] - (v[a] + v[b]) / 2.0f);
    }
    std::vector<int> baseIds(ids.rbegin(), ids.rend());
    const int f = m.addFace("base", baseIds, inside);
    setFrame(m, f, QVector3D(-width / 2.0f, 0.0f, -depth / 2.0f), QVector3D(width, 0.0f, 0.0f),
             QVector3D(0.0f, 0.0f, depth));
    m.buildEdges();
    return solid;
}

Solid cylinder(float radius, float height, int segments)
{
    Solid solid;
    solid.kind = Solid::Kind::Cylinder;
    solid.radius = radius;
    solid.height = height;
    Mesh &m = solid.mesh;
    const std::vector<int> bottom = ring(m, radius, 0.0f, segments);
    const std::vector<int> top = ring(m, radius, height, segments);
    const QVector3D inside(0.0f, height / 2.0f, 0.0f);
    for (int i = 0; i < segments; ++i) {
        const int j = (i + 1) % segments;
        m.addFace(QString(), {bottom[i], bottom[j], top[j], top[i]}, inside);
    }
    cap(m, "topo", top, radius, height, inside);
    cap(m, "base", bottom, radius, 0.0f, inside);
    return solid;
}

Solid cone(float radius, float height, int segments)
{
    Solid solid;
    solid.kind = Solid::Kind::Cone;
    solid.radius = radius;
    solid.height = height;
    Mesh &m = solid.mesh;
    const std::vector<int> bottom = ring(m, radius, 0.0f, segments);
    const int apex = m.addVertex(QVector3D(0.0f, height, 0.0f));
    const QVector3D inside(0.0f, height / 4.0f, 0.0f);
    for (int i = 0; i < segments; ++i)
        m.addFace(QString(), {bottom[i], bottom[(i + 1) % segments], apex}, inside);
    cap(m, "base", bottom, radius, 0.0f, inside);
    return solid;
}

Solid sphere(float radius, int segments, int rings)
{
    Solid solid;
    solid.kind = Solid::Kind::Sphere;
    solid.radius = radius;
    solid.base = QVector3D(0.0f, radius, 0.0f); // centro: a esfera se apoia em y = 0
    Mesh &m = solid.mesh;
    const QVector3D center = solid.base;

    // Paralelos entre os polos; faces entre paralelos vizinhos são trapézios planos
    std::vector<std::vector<int>> latitudes;
    for (int k = 1; k < rings; ++k) {
        const float polar = kPi * float(k) / float(rings);
        latitudes.push_back(ring(m, radius * std::sin(polar), 0.0f, segments));
        for (int id : latitudes.back())
            m.vertices[id] += center + QVector3D(0.0f, radius * std::cos(polar), 0.0f);
    }
    const int north = m.addVertex(center + QVector3D(0.0f, radius, 0.0f));
    const int south = m.addVertex(center - QVector3D(0.0f, radius, 0.0f));
    for (int i = 0; i < segments; ++i) {
        const int j = (i + 1) % segments;
        m.addFace(QString(), {latitudes.front()[i], latitudes.front()[j], north}, center);
        m.addFace(QString(), {latitudes.back()[j], latitudes.back()[i], south}, center);
        for (std::size_t k = 0; k + 1 < latitudes.size(); ++k)
            m.addFace(QString(), {latitudes[k][i], latitudes[k][j], latitudes[k + 1][j], latitudes[k + 1][i]}, center);
    }
    return solid;
}

} // namespace solids
