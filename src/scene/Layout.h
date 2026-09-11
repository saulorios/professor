#pragma once

#include "SceneParams.h"

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

// Elemento já desenhado na lousa (a cena guarda todos, com ou sem id)
struct SceneElement {
    enum class Kind { Ellipse, Box };   // formato usado para achar a borda em "conectar"
    QString id;
    QString label;                      // id, ou uma descrição curta (modo de depuração)
    Kind kind = Kind::Box;
    QRectF bounds;                      // bounding box em unidades da lousa
    QPointF anchor;                     // ponto de referência (centro do círculo, do texto…)
    bool obstacle = true;               // linhas, setas, conexões e destaques não bloqueiam
};

// Resolve as formas de posicionamento do protocolo (seção 3), em unidades da
// lousa: "ancora", "abaixo_de"/"acima_de"/"direita_de"/"esquerda_de" (com
// "margem" e "alinhar"), "relativo_a" (com "angulo" e "distancia"),
// "em_centro_de" e "em".
//
// - A área útil fica a `margin` das bordas; embaixo, o limite é a faixa
//   reservada à legenda da fala, e não a borda da lousa.
// - Elemento fora da área útil é empurrado para dentro (com aviso no log).
// - Colisão com a bounding box de outro elemento: desloca na direção do
//   posicionamento até não colidir (até `maxCollisionAttempts` tentativas;
//   depois aceita, com aviso). "em" e "em_centro_de" não são deslocados.
// - Referência a id inexistente: usa a âncora "centro" (com aviso).
class Layout
{
public:
    explicit Layout(const SceneParams &params);

    // Área útil: lousa menos as margens e a faixa da legenda
    QRectF usableArea() const;

    // Deslocamento a aplicar à geometria montada em torno de (0,0). `local` é a
    // bounding box dessa geometria e `localAnchor`, o seu ponto de referência.
    QPointF place(const QJsonObject &command, const QRectF &local, const QPointF &localAnchor,
                  const std::vector<SceneElement> &elements) const;

    // Elemento com o id (o mais recente), ou nullptr
    static const SceneElement *find(const std::vector<SceneElement> &elements, const QString &id);

private:
    // Deslocamento da âncora nomeada; `direction` recebe o sentido de afastamento
    QPointF anchorOffset(const QString &anchor, const QRectF &local, QPointF *direction) const;
    QRectF pushInside(const QRectF &box) const;

    SceneParams m_params;
};
