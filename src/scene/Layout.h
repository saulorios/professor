#pragma once

#include "Occupancy.h"
#include "SceneParams.h"
#include "hand/Polyline.h"

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

// Um elemento já desenhado na lousa
struct SceneElement {
    enum class Kind { Ellipse, Box };

    QString id;
    QString label;                // id, ou uma descrição para os elementos sem id
    Kind kind = Kind::Box;        // como a borda é calculada em "conectar"
    QRectF bounds;                // em unidades do canvas
    QPointF anchor;               // ponto de referência (centro do círculo, do texto...)
    bool obstacle = true;         // a caixa inteira é obstáculo
    std::vector<Polyline> marks;  // quando a caixa não vale: o traço em si (linhas, setas)
};

// Motor de layout. O padrão é o FLUXO: quem não traz posicionamento vai abaixo
// do anterior, como num documento, e o canvas rola (e cresce) quando a tela
// enche. Posicionar é a exceção.
//
// Sobreposição nunca é aceitável: toda colocação passa pela grade de ocupação e,
// se o lugar pedido estiver tomado, o elemento é deslocado, mandado para o
// espaço livre mais próximo, para uma tela nova, e só em último caso reduzido.
class Layout
{
public:
    // Papel do elemento no fluxo (títulos são centralizados)
    enum class Role { Body, Title };

    struct Placement {
        QPointF offset;      // aplicado depois da escala, em torno do ponto de referência
        double scale = 1.0;  // < 1 só quando nem numa tela vazia coube
    };

    explicit Layout(const SceneParams &params);

    // Área útil de uma tela do canvas (a faixa da legenda não entra: é overlay)
    QRectF screenArea(int screen) const;
    QRectF usableArea() const { return screenArea(m_screen); }
    QRectF canvasArea() const;

    int screen() const { return m_screen; }
    int screenCount() const { return m_screens; }
    double canvasHeight() const { return m_screens * m_params.boardHeight; }

    // Faz o canvas caber esta área, para geometria que não passa pelo layout
    // (traços gravados, que já vêm em coordenadas do canvas)
    void include(const QRectF &area);

    // Comandos de fluxo
    void reset();
    void newLine();
    bool setColumn(const QString &which); // "esquerda", "direita" ou "unica"
    void newScreen();

    Placement place(const QJsonObject &command, const QRectF &local, const QPointF &localAnchor,
                    const std::vector<SceneElement> &elements, Role role = Role::Body);

    static const SceneElement *find(const std::vector<SceneElement> &elements, const QString &id);

private:
    QRectF columnRect(int screen, int column) const;
    // Tela nova por falta de espaço: mantém as colunas que a aula pediu
    void overflowScreen();
    int screenOf(const QRectF &box) const;
    void growTo(int screen);
    // Refaz a grade a partir dos elementos, menos o que pode ser tocado de propósito
    void rebuildGrid(const std::vector<SceneElement> &elements, const SceneElement *allowed);
    QPointF anchorOffset(const QString &anchor, const QRectF &local, QPointF *direction) const;
    QRectF pushInside(const QRectF &box, const QRectF &area) const;
    // Fluxo: onde entra um elemento deste tamanho
    QPointF flowOrigin(const QRectF &local, Role role);
    // Espaço livre mais próximo do desejado, dentro da área
    bool nearestFree(const QRectF &area, const QSizeF &size, const QPointF &wanted, QPointF *found) const;
    void noteOccupied(const QRectF &box);

    SceneParams m_params;
    Occupancy m_grid;
    int m_screens = 1;
    int m_screen = 0;
    int m_column = 0;          // 0 = única/esquerda, 1 = direita
    bool m_twoColumns = false;
    bool m_columnChosen = false; // a aula escolheu a coluna: não trocar por conta própria
    double m_pen[2] = {0.0, 0.0};
};
