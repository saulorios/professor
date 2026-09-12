#pragma once

#include "Geometry2D.h"
#include "HersheyFont.h"
#include "Humanizer.h"
#include "Layout.h"
#include "Object3D.h"
#include "SceneParams.h"
#include "TextLayout.h"
#include "hand/VirtualHand.h"

#include <QJsonObject>
#include <QObject>
#include <QRectF>
#include <QString>

#include <vector>

// Geometria extra do modo de depuração (F12), em unidades da lousa
struct SceneDebugGeometry {
    std::vector<Polyline> hiddenLines;    // arestas ocultas dos objetos 3D
    std::vector<Polyline> vanishingLines; // linhas finas até os pontos de fuga
    std::vector<QPointF> vanishingPoints;
};

// Guarda os elementos desenhados (com ou sem id) e executa os comandos que
// desenham ou apagam: resolve o posicionamento (Layout), gera a geometria e
// entrega as polilinhas à mão virtual. Emite finished() quando o comando termina.
class Scene : public QObject
{
    Q_OBJECT

public:
    explicit Scene(VirtualHand &hand, const SceneParams &params = SceneParams(), QObject *parent = nullptr);

    // Executa "forma", "escrever", "conectar", "destacar", "objeto_3d",
    // "rotular", "cotar", "traco_livre", "linha", "coluna", "nova_tela",
    // "apagar" ou "limpar"
    void execute(const QJsonObject &command);

    // Esquece todos os elementos e interrompe o desenho em andamento
    void reset();

    const SceneParams &params() const { return m_params; }

    // Humanização da escrita (vale a partir do próximo texto)
    void setHumanizerParams(const HumanizerParams &params) { m_humanizer.setParams(params); }
    const HumanizerParams &humanizerParams() const { return m_humanizer.params(); }
    const std::vector<SceneElement> &elements() const { return m_elements; }
    QRectF usableArea() const { return m_layout.usableArea(); }
    double canvasHeight() const { return m_layout.canvasHeight(); }
    bool contains(const QString &id) const { return Layout::find(m_elements, id) != nullptr; }
    QRectF bounds(const QString &id) const;
    // Leva o fluxo para uma tela limpa se a atual já tiver conteúdo; devolve o
    // topo da área útil onde a próxima resposta vai começar (unidades)
    double startFreshScreen();
    SceneDebugGeometry debugGeometry() const;

signals:
    void finished();
    void elementsChanged();
    // O canvas precisa ter esta altura (unidades): a lousa cresce para baixo
    void canvasHeightChanged(double units);
    // O motor quer que esta faixa do canvas (unidades) esteja à vista
    void ensureVisible(const QRectF &area);

private:
    void drawShape(const QJsonObject &command);
    void writeText(const QJsonObject &command);
    void connectElements(const QJsonObject &command);
    void highlight(const QJsonObject &command);
    void drawFreeStroke(const QJsonObject &command);
    void drawObject(const QJsonObject &command);
    void labelVertex(const QJsonObject &command);
    void dimensionEdge(const QJsonObject &command);
    void flowCommand(const QJsonObject &command);
    void eraseElement(const QJsonObject &command);
    void clearAll();

    // Depois de colocar um elemento: cresce o canvas e leva a vista até ele
    void reveal(const QRectF &bounds);

    // Texto já quebrado em linhas e humanizado, com a linha de base da primeira
    // linha em y = 0 e começando em x = 0. `lines` recebe a caixa de cada linha.
    std::vector<HandStroke> writing(const QString &text, double size, bool cursive, PressureLevel pressure,
                                    double maxWidth, std::vector<QRectF> *lines = nullptr) const;

    const Object3DInfo *findObject(const QString &id) const;
    void forgetObject(const QString &id);

    // "de"/"ate": ponto [x,y] ou id de elemento
    bool endpoint(const QJsonValue &value, QPointF *point, const SceneElement **element) const;
    QPointF borderPoint(const SceneElement &element, const QPointF &toward) const;
    // Registra o elemento (o id vem do comando; `label` descreve os sem id)
    void store(const QJsonObject &command, SceneElement element, const QString &label);
    // Estiliza as linhas e entrega à mão (as `solid` ignoram o estilo, ex.: ponta de seta)
    void submit(const QJsonObject &command, const std::vector<Polyline> &lines,
                const std::vector<Polyline> &solid = {});

    void finishLater();
    void fail(const QString &message);

    SceneParams m_params;
    Geometry2D m_geometry;
    Layout m_layout;
    HersheyFont m_font;          // "fonte": "normal"
    HersheyFont m_cursiveFont;   // "fonte": "cursiva"
    TextLayout m_textLayout;
    TextLayout m_cursiveLayout;
    Humanizer m_humanizer;
    Object3DBuilder m_builder3D;
    VirtualHand &m_hand;
    std::vector<SceneElement> m_elements; // na ordem em que foram desenhados
    std::vector<Object3DInfo> m_objects;  // objetos 3D vivos (para rotular e cotar)
    bool m_waitingHand = false;
    int m_generation = 0;         // invalida finalizações pendentes após reset()
};
