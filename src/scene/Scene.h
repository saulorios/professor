#pragma once

#include "Geometry2D.h"
#include "HersheyFont.h"
#include "SceneParams.h"
#include "TextLayout.h"
#include "hand/VirtualHand.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QRectF>
#include <QString>

// Elemento já desenhado na lousa
struct SceneElement {
    enum class Kind { Ellipse, Box };   // formato usado para achar a borda em "conectar"
    Kind kind = Kind::Box;
    QRectF bounds;                      // bounding box em unidades da lousa
};

// Guarda os elementos por id e executa os comandos que desenham ou apagam:
// resolve o posicionamento ("em" ou "ancora"), gera a geometria e entrega as
// polilinhas à mão virtual. Emite finished() quando o comando termina.
class Scene : public QObject
{
    Q_OBJECT

public:
    explicit Scene(VirtualHand &hand, const SceneParams &params = SceneParams(), QObject *parent = nullptr);

    // Executa "forma", "escrever", "conectar", "apagar" ou "limpar"
    void execute(const QJsonObject &command);

    // Esquece todos os elementos e interrompe o desenho em andamento
    void reset();

    bool contains(const QString &id) const { return m_elements.contains(id); }
    QRectF bounds(const QString &id) const { return m_elements.value(id).bounds; }

signals:
    void finished();

private:
    void drawShape(const QJsonObject &command);
    void writeText(const QJsonObject &command);
    void connectElements(const QJsonObject &command);
    void eraseElement(const QJsonObject &command);
    void clearAll();

    // Ponto de origem do elemento pelo posicionamento; `local` é a bounding box em torno de (0,0)
    QPointF placement(const QJsonObject &command, const QRectF &local) const;
    // "de"/"ate": ponto [x,y] ou id de elemento
    bool endpoint(const QJsonValue &value, QPointF *point, const SceneElement **element) const;
    QPointF borderPoint(const SceneElement &element, const QPointF &toward) const;
    void store(const QJsonObject &command, const SceneElement &element);
    // Estiliza as linhas e entrega à mão (as `solid` ignoram o estilo, ex.: ponta de seta)
    void submit(const QJsonObject &command, const std::vector<Polyline> &lines,
                const std::vector<Polyline> &solid = {});

    void finishLater();
    void fail(const QString &message);

    SceneParams m_params;
    Geometry2D m_geometry;
    HersheyFont m_font;
    TextLayout m_textLayout;
    VirtualHand &m_hand;
    QHash<QString, SceneElement> m_elements;
    bool m_waitingHand = false;
    int m_generation = 0;         // invalida finalizações pendentes após reset()
};
