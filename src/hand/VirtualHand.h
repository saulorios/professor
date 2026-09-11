#pragma once

#include "HandParams.h"
#include "Polyline.h"
#include "physics/Eraser.h"
#include "physics/StrokeEngine.h"

#include <QElapsedTimer>
#include <QObject>
#include <QRectF>
#include <QTimer>

#include <cstddef>
#include <vector>

class Board;

// Pressão base pedida pelo comando ("pressao": "leve" | "normal" | "forte")
enum class PressureLevel { Light, Normal, Strong };

// Tipo de movimento: formas ou escrita (a escrita é um pouco mais rápida)
enum class Motion { Shape, Writing };

// "Mão do professor": recebe polilinhas já posicionadas (unidades da lousa) e
// as reproduz ao longo do tempo como ChalkSamples, imitando uma mão humana:
// acelera no início, freia em curvas fechadas e no fim, pressiona mais no meio
// do traço, treme levemente e faz uma pausa curta entre os traços.
// Roda num QTimer de ~60 Hz e alimenta o mesmo StrokeEngine/Eraser do mouse.
// Os timestamps entregues à física são do tempo simulado da mão, então a
// aparência do giz não muda com a velocidade de reprodução.
class VirtualHand : public QObject
{
    Q_OBJECT

public:
    explicit VirtualHand(Board &board, const HandParams &params = HandParams(), QObject *parent = nullptr);

    // Desenha as polilinhas, um traço de giz por polilinha, na ordem recebida
    void draw(const std::vector<Polyline> &strokes, PressureLevel pressure, Motion motion = Motion::Shape);

    // Passa o apagador em zigue-zague sobre a área
    void erase(const QRectF &area);

    // Limpa a lousa inteira de uma vez
    void clearBoard();

    // Interrompe o trabalho atual sem emitir finished()
    void cancel();

    void setPaused(bool paused);
    void setSpeed(double factor);
    bool isBusy() const { return m_busy; }

signals:
    void finished();      // o trabalho atual terminou de ser desenhado
    void boardChanged();  // o depósito de giz foi alterado

private:
    enum class Tool { Chalk, Eraser };

    struct TimedSample {
        QPointF pos;      // px da lousa
        float pressure;
        double timeMs;    // relativo ao início do trabalho
        bool first;       // começo de traço (giz encosta)
        bool last;        // fim de traço (giz levanta)
    };

    void beginJob(Tool tool, double penLiftMs);
    void startPlayback();
    void finishLater();
    void appendStroke(const Polyline &units, double speed, float basePressure, bool wobble);
    void resample(const Polyline &units);
    void deliver(const TimedSample &sample);
    void tick();

    HandParams m_params;
    Board &m_board;
    StrokeEngine m_stroke;
    Eraser m_eraser;
    QSizeF m_boardUnits;

    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_speed = 1.0;
    bool m_paused = false;
    bool m_busy = false;
    int m_generation = 0;         // invalida finalizações pendentes após cancel()

    Tool m_tool = Tool::Chalk;
    double m_penLift = 0.0;       // tempo de levantar o giz no trabalho atual (ms)
    std::vector<TimedSample> m_plan;
    std::size_t m_next = 0;
    double m_playhead = 0.0;      // tempo simulado do trabalho atual (ms)
    double m_planEnd = 0.0;
    double m_cursor = 0.0;        // tempo usado durante a montagem do plano
    double m_jobStart = 0.0;      // tempo simulado absoluto do início do trabalho
    double m_simTime = 0.0;       // tempo simulado acumulado da mão
    bool m_strokeOpen = false;
    int m_strokeCounter = 0;      // semente do tremor de cada traço
    QPointF m_handPos;            // última posição da mão (unidades)

    // Memória reaproveitada entre traços (sem alocação a cada traço)
    std::vector<QPointF> m_points;
    std::vector<double> m_dist;
    std::vector<double> m_vel;
};
