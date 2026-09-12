#include "Projection.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.141592653589793;
constexpr float kEpsilon = 1e-6f;

double radians(double degrees)
{
    return degrees * kPi / 180.0;
}

} // namespace

Projection::Projection(View view, const SceneParams &params, const std::vector<QVector3D> &points,
                       double rotationDeg, double eyeFactor)
    : m_view(view)
{
    if (view == View::Cavalier) {
        const double a = radians(params.cavalierAngle);
        const auto k = float(params.cavalierReduction);
        m_rowX = QVector3D(1.0f, 0.0f, k * float(std::cos(a)));
        m_rowY = QVector3D(0.0f, 1.0f, k * float(std::sin(a)));
    } else if (view == View::Isometric) {
        // Eixos a 30°: +x desce para a direita, +z (para longe) sobe para a direita
        const auto c = float(std::cos(radians(params.isometricAngle)));
        const auto s = float(std::sin(radians(params.isometricAngle)));
        m_rowX = QVector3D(c, 0.0f, c);
        m_rowY = QVector3D(-s, 1.0f, s);
    }
    if (!isPerspective()) {
        // Direção do observador: normal do plano da tela, do lado de quem olha (z < 0)
        QVector3D dir = QVector3D::crossProduct(m_rowX, m_rowY);
        if (dir.z() > 0.0f)
            dir = -dir;
        m_viewDir = dir.normalized();
        return;
    }

    // Perspectiva: o objeto gira em torno de Y (só na de 2 pontos) e a câmera
    // olha na horizontal, na direção +z
    QVector3D low = points.empty() ? QVector3D() : points.front();
    QVector3D high = low;
    for (const QVector3D &p : points) {
        low = QVector3D(std::min(low.x(), p.x()), std::min(low.y(), p.y()), std::min(low.z(), p.z()));
        high = QVector3D(std::max(high.x(), p.x()), std::max(high.y(), p.y()), std::max(high.z(), p.z()));
    }
    const QVector3D center = (low + high) / 2.0f;

    QMatrix4x4 model;
    model.translate(center);
    if (view == View::TwoPoint)
        model.rotate(float(rotationDeg), 0.0f, 1.0f, 0.0f); // positivo mostra a frente e a direita
    model.translate(-center);

    // Raio da esfera que envolve o objeto girado e distância para o campo de visão pedido
    float radius = 0.0f;
    for (const QVector3D &p : points)
        radius = std::max(radius, (model.map(p) - center).length());
    const auto half = float(radians(params.fieldOfView / 2.0));
    const float distance = radius > 0.0f ? radius / std::max(std::sin(half), 0.05f) : 1.0f;

    const float eyeY = low.y() + float(eyeFactor) * (high.y() - low.y());
    const QVector3D eyeWorld(center.x(), eyeY, center.z() - distance);

    // Pinhole: x' = x/z, y' = y/z (a escala final é normalizada pelo Object3D)
    QMatrix4x4 pinhole(1.0f, 0.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f, 0.0f,
                       0.0f, 0.0f, 1.0f, 0.0f,
                       0.0f, 0.0f, 1.0f, 0.0f);
    QMatrix4x4 toEye;
    toEye.translate(-eyeWorld);
    m_matrix = pinhole * toEye * model;
    m_eye = model.inverted().map(eyeWorld); // olho em coordenadas do modelo
}

QPointF Projection::project(const QVector3D &p) const
{
    if (!isPerspective()) {
        // Y da lousa é para baixo
        return QPointF(QVector3D::dotProduct(m_rowX, p), -QVector3D::dotProduct(m_rowY, p));
    }
    const QVector4D v = m_matrix * QVector4D(p, 1.0f);
    const float w = std::abs(v.w()) > kEpsilon ? v.w() : kEpsilon;
    return QPointF(v.x() / w, -v.y() / w);
}

QVector3D Projection::towardViewer(const QVector3D &p) const
{
    if (!isPerspective())
        return m_viewDir;
    const QVector3D d = m_eye - p;
    const float len = d.length();
    return len > kEpsilon ? d / len : m_viewDir;
}

bool Projection::vanishingPoint(const QVector3D &dir, QPointF *out) const
{
    if (!isPerspective())
        return false;
    const QVector4D v = m_matrix * QVector4D(dir, 0.0f);
    if (std::abs(v.w()) < 1e-4f)
        return false; // direção paralela ao plano da tela: as retas ficam paralelas
    *out = QPointF(v.x() / v.w(), -v.y() / v.w());
    return true;
}
