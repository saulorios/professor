#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <deque>

class Scene;

// Fila de comandos: executa um por vez e só avança quando o anterior terminou
// de ser desenhado. "fala" não bloqueia; "pausa" espera (na velocidade atual);
// os demais comandos suportados vão para a cena.
class CommandQueue : public QObject
{
    Q_OBJECT

public:
    explicit CommandQueue(Scene &scene, QObject *parent = nullptr);

    void enqueue(const QJsonObject &command);

    // Descarta os comandos pendentes e interrompe a pausa em andamento
    void clear();

    void setPaused(bool paused);
    void setSpeed(double factor);

    bool isIdle() const { return !m_busy && m_pending.empty(); }

signals:
    void speech(const QString &text);
    void idle(); // a fila esvaziou e o último comando terminou

private:
    void startNext();
    void commandFinished();
    void finishLater();
    void startPauseTimer();

    Scene &m_scene;
    std::deque<QJsonObject> m_pending;
    bool m_busy = false;
    bool m_paused = false;
    double m_speed = 1.0;
    int m_generation = 0;         // invalida finalizações pendentes após clear()

    // Comando "pausa"
    QTimer m_pauseTimer;
    QElapsedTimer m_pauseClock;
    double m_pauseRemainingMs = 0.0;
    bool m_inPause = false;
};
