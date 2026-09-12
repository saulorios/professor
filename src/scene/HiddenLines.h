#pragma once

#include "Projection.h"
#include "SceneParams.h"
#include "hand/Polyline.h"
#include "solids/Mesh.h"

#include <vector>

// Decide o que fica escondido no objeto 3D projetado.
//
// Uma face é visível quando a normal aponta para o observador. As faces
// visíveis de cada parte viram oclusores: um ponto é ocultado quando a sua
// projeção cai dentro do polígono de uma face visível de OUTRA parte e o raio
// que vai do ponto até o observador atravessa o plano dessa face.
// A auto-oclusão de cada parte é resolvida por fora (nos poliedros convexos,
// uma aresta é visível se pelo menos uma das faces vizinhas está de frente).
class HiddenLines
{
public:
    HiddenLines(const Projection &projection, const SceneParams &params);

    // Registra as faces visíveis da malha como oclusores da parte `part`.
    // `skip` (opcional) marca as faces internas, que não escondem nada.
    void addPart(int part, const Mesh &mesh, const std::vector<bool> *skip = nullptr);

    bool frontFacing(const Mesh &mesh, const MeshFace &face) const;
    // `parts` são as partes donas da linha (mais de uma quando arestas
    // coincidentes foram fundidas); as faces delas não escondem a própria linha
    bool occludedByOthers(const QVector3D &p, const std::vector<int> &parts) const;

    // Trecho de uma linha 3D já projetado, todo visível ou todo oculto
    struct Piece {
        Polyline points;
        bool visible = true;
    };
    // Quebra a linha 3D em trechos visíveis e ocultos. `selfVisible` diz se a
    // linha é visível dentro da própria parte (auto-oclusão já resolvida).
    std::vector<Piece> split(const std::vector<QVector3D> &path, const std::vector<int> &parts,
                             bool selfVisible) const;

private:
    struct Occluder {
        int part = -1;
        QVector3D normal;
        float offset = 0.0f;   // plano: normal · x = offset
        Polyline polygon;      // face projetada
    };

    const Projection &m_projection;
    SceneParams m_params;
    std::vector<Occluder> m_occluders;
};
