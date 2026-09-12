#pragma once

#include "ChalkPose.h"
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

// Um traço com o seu próprio ritmo: usado pela escrita humanizada (cada letra
// sai um pouco mais rápida ou mais leve) e pelas arestas ocultas dos objetos 3D
struct HandStroke {
    Polyline points;
    PressureLevel pressure = PressureLevel::Normal;
    double speedScale = 1.0;      // 1 = a velocidade normal do movimento
    double pressureScale = 1.0;   // multiplica a pressão base
    double pauseBeforeMs = 0.0;   // micro-pausa antes deste traço
};

// Amostra de um traço gravado (comando "traco_livre"): posição em unidades da
// lousa, pressão e o instante em que foi feita, contado do início do traço
struct RecordedPoint {
    QPointF pos;
    float pressure = 0.6f;
    double timeMs = 0.0;
};

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

    // Mesma coisa, com uma pressão por traço (ex.: arestas ocultas mais leves)
    void draw(const std::vector<Polyline> &strokes, const std::vector<PressureLevel> &pressures,
              Motion motion = Motion::Shape);

    // Cada traço com o seu ritmo (escrita humanizada)
    void draw(const std::vector<HandStroke> &strokes, Motion motion = Motion::Shape);

    // Refaz um traço gravado com o tempo e a pressão originais: sem perfil de
    // velocidade, sem tremor, sem reamostragem
    void drawRecorded(const std::vector<RecordedPoint> &points);

    // Passa o apagador em zigue-zague sobre a área
    void erase(const QRectF &area);

    // Limpa a lousa inteira de uma vez
    void clearBoard();

    // Interrompe o trabalho atual sem emitir finished()
    void cancel();

    void setPaused(bool paused);
    void setSpeed(double factor);

    // Novos parâmetros valem a partir do próximo trabalho montado; a conversão
    // unidades → px (pixelsPerUnit) acompanha a resolução fixa da lousa
    void setParams(const HandParams &params);
    const HandParams &params() const { return m_params; }
    bool isBusy() const { return m_busy; }

signals:
    void finished();      // o trabalho atual terminou de ser desenhado
    void boardChanged();  // o depósito de giz foi alterado
    void chalkMoved(const ChalkPose &pose); // onde desenhar o giz na tela
    void chalkHidden();                     // a mão saiu de cena (fim, pausa ou apagador)

private:
    enum class Tool { Chalk, Eraser };

    struct TimedSample {
        QPointF pos;      // px da lousa
        float pressure;
        double timeMs;    // relativo ao início do trabalho
        bool first;       // começo de traço (giz encosta)
        bool last;        // fim de traço (giz levanta)
    };

    float basePressure(PressureLevel level) const;
    void beginJob(Tool tool, double penLiftMs);
    void startPlayback();
    void finishLater();
    void appendStroke(const Polyline &units, double speed, float basePressure, bool wobble);
    void resample(const Polyline &units);
    void deliver(const TimedSample &sample);
    void tick();
    void publishPose();

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
    double m_heading = 0.0;       // direção suavizada do giz na tela (radianos)
    bool m_headingReady = false;  // o primeiro traço não interpola: já nasce na direção certa
    int m_strokeCounter = 0;      // semente do tremor de cada traço
    QPointF m_handPos;            // última posição da mão (unidades)

    // Memória reaproveitada entre traços (sem alocação a cada traço)
    std::vector<QPointF> m_points;
    std::vector<double> m_dist;
    std::vector<double> m_vel;
};
