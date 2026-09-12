#pragma once

#include "Mesh.h"

// Geradores dos sólidos do comando "objeto_3d". Eixos do modelo: X para a
// direita, Y para cima, Z para longe do observador (a face "frente" fica em
// z mínimo). Cada sólido nasce apoiado em y = 0 e centrado em x e z; a
// "posicao" da parte é aplicada depois com Solid::translate().
//
// Nomes dos vértices da caixa: frente_base_esquerda, frente_base_direita,
// frente_topo_direita, frente_topo_esquerda e os mesmos com tras_.
namespace solids {

Solid box(float width, float height, float depth);
// Triângulo na face da frente (base embaixo, vértice "frente_topo" em cima),
// extrudado em profundidade até "tras_topo"
Solid triangularPrism(float width, float height, float depth);
// Base regular com `sides` lados (3 a 8) ajustada ao retângulo width × depth e
// ápice "topo"; sempre há uma aresta da base de frente para o observador
Solid pyramid(float width, float depth, float height, int sides);
// Sólidos curvos: a malha facetada (segments ao redor) serve só para a
// oclusão de outras partes e para as medidas; o desenho usa as silhuetas
Solid cylinder(float radius, float height, int segments);
Solid cone(float radius, float height, int segments);
Solid sphere(float radius, int segments, int rings);

} // namespace solids
