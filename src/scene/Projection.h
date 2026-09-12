#pragma once

#include "SceneParams.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QVector3D>

#include <vector>

// Projeta pontos do modelo 3D para o plano da lousa, UMA única vez (não há
// câmera em tempo real). Y do modelo é para cima; o Y projetado já sai virado
// para baixo, como o da lousa.
//
// - Cavaleira: x' = x + z·reducao·cos(angulo), y' = y + z·reducao·sin(angulo)
// - Isométrica: ortográfica com os eixos a 30°
// - Perspectiva de 1 e 2 pontos: câmera pinhole (QMatrix4x4); "rotacao" gira o
//   objeto em torno de Y e "altura_olho" põe o olho abaixo, no meio ou acima.
//   A distância vem do campo de visão (fieldOfView), para não distorcer demais.
class Projection
{
public:
    enum class View { Cavalier, Isometric, OnePoint, TwoPoint };

    Projection() = default;
    // `bounds` são os pontos do objeto (para centrar e afastar a câmera)
    Projection(View view, const SceneParams &params, const std::vector<QVector3D> &points,
               double rotationDeg, double eyeFactor);

    View view() const { return m_view; }
    bool isPerspective() const { return m_view == View::OnePoint || m_view == View::TwoPoint; }

    QPointF project(const QVector3D &p) const;
    // Direção que aponta do ponto para o observador (unitária)
    QVector3D towardViewer(const QVector3D &p) const;
    // Ponto de fuga da direção `dir` (falso se ela for paralela ao plano da tela)
    bool vanishingPoint(const QVector3D &dir, QPointF *out) const;
    // Olho da câmera em coordenadas do modelo (só faz sentido em perspectiva)
    const QVector3D &eye() const { return m_eye; }

private:
    View m_view = View::Cavalier;
    // Projeção paralela: linhas da matriz 2×3
    QVector3D m_rowX{1.0f, 0.0f, 0.0f};
    QVector3D m_rowY{0.0f, 1.0f, 0.0f};
    QVector3D m_viewDir{0.0f, 0.0f, -1.0f}; // do objeto para o observador
    // Perspectiva
    QMatrix4x4 m_matrix;
    QVector3D m_eye;
};
