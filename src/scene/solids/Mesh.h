#pragma once

#include <QString>
#include <QVector3D>

#include <vector>

// Face plana e convexa do modelo 3D
struct MeshFace {
    QString name;                // frente, tras, esquerda, direita, topo, base (ou vazio)
    std::vector<int> vertices;   // índices, em ordem ao redor da face
    QVector3D normal;            // unitária, para fora do sólido
    // Referencial dos decalques: P(u, v) = origin + u·uAxis + v·vAxis, com u e v
    // em 0..1 (u da esquerda para a direita vista de fora, v = 0 na base)
    QVector3D origin;
    QVector3D uAxis;
    QVector3D vAxis;
};

struct MeshEdge {
    int a = -1;                  // vértices
    int b = -1;
    int faceA = -1;              // faces adjacentes (-1 se não houver)
    int faceB = -1;
};

// Malha simples: vértices, faces e arestas (cada aresta aparece uma vez)
struct Mesh {
    std::vector<QVector3D> vertices;
    std::vector<QString> vertexNames;   // nome semântico ou vazio
    std::vector<MeshFace> faces;
    std::vector<MeshEdge> edges;

    int addVertex(const QVector3D &p, const QString &name = QString());
    // Adiciona uma face; a normal é calculada e orientada para longe de `inside`
    int addFace(const QString &name, std::vector<int> vertexIds, const QVector3D &inside);
    // Monta as arestas a partir das faces (as compartilhadas entram uma vez só)
    void buildEdges();
    void translate(const QVector3D &offset);

    int findVertex(const QString &name) const;
    int findFace(const QString &name) const;
    QVector3D centroid(const MeshFace &face) const;
};

// Uma parte do objeto: poliedro (desenhado pelas arestas) ou sólido curvo
// (desenhado pela silhueta; a malha facetada serve só para a oclusão)
struct Solid {
    enum class Kind { Polyhedron, Cylinder, Cone, Sphere };

    Kind kind = Kind::Polyhedron;
    QString id;
    Mesh mesh;
    QVector3D base;       // curvos: centro da base (esfera: centro)
    float radius = 0.0f;
    float height = 0.0f;

    void translate(const QVector3D &offset);
    // Ponto com nome: vértice do poliedro ou ponto notável do sólido curvo
    bool namedPoint(const QString &name, QVector3D *out) const;
};
