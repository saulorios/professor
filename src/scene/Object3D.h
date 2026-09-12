#pragma once

#include "Geometry2D.h"
#include "Layout.h"
#include "Projection.h"
#include "SceneParams.h"
#include "TextLayout.h"
#include "hand/VirtualHand.h"

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

// Um traço do objeto 3D já pronto para a mão
struct ObjectStroke {
    Polyline points;
    PressureLevel pressure = PressureLevel::Normal;
};

// Aresta reta do objeto, projetada (para "cotar" e para a depuração)
struct Object3DEdge {
    QPointF a;
    QPointF b;
    double length3D = 0.0;  // comprimento real, para o texto padrão da cota
    int axis = -1;          // 0 = largura (X), 1 = altura (Y), 2 = profundidade (Z)
    bool visible = true;
};

// Vértice com nome ("frente_topo_direita"), projetado (para "rotular")
struct Object3DVertex {
    QString part;
    QString name;
    QPointF at;
};

// Tudo o que a cena precisa saber do objeto depois de desenhado
struct Object3DInfo {
    QString id;
    QRectF bounds;
    Polyline hull;                        // silhueta convexa do objeto projetado
    std::vector<Object3DVertex> vertices;
    std::vector<Object3DEdge> edges;
    std::vector<Polyline> hiddenLines;    // sempre preenchidas (F12), mesmo com "omitir"
    std::vector<QPointF> vanishingPoints;
    std::vector<Polyline> vanishingLines;
};

// Monta o objeto 3D descrito pelo comando "objeto_3d": gera os sólidos, projeta
// uma única vez, separa o que fica oculto, aplica escala e layout e devolve
// polilinhas comuns, na ordem em que um professor desenharia (face da frente,
// arestas de profundidade, demais arestas, ocultas tracejadas e decalques).
class Object3DBuilder
{
public:
    Object3DBuilder(const SceneParams &params, const Layout &layout, const TextLayout &text);

    bool build(const QJsonObject &command, const std::vector<SceneElement> &elements,
               PressureLevel pressure, std::vector<ObjectStroke> *strokes, Object3DInfo *info,
               QString *error) const;

private:
    SceneParams m_params;
    Geometry2D m_geometry;
    const Layout &m_layout;
    const TextLayout &m_text;
};
