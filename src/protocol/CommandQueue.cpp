#include "CommandQueue.h"

#include "scene/Scene.h"

#include <QDebug>

#include <algorithm>

CommandQueue::CommandQueue(Scene &scene, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
{
    m_pauseTimer.setSingleShot(true);
    connect(&m_pauseTimer, &QTimer::timeout, this, [this] {
        m_inPause = false;
        commandFinished();
    });
    connect(&m_scene, &Scene::finished, this, [this] {
        if (!m_inPause)
            commandFinished();
    });
}

void CommandQueue::enqueue(const QJsonObject &command)
{
    m_pending.push_back(command);
    startNext();
}

void CommandQueue::clear()
{
    ++m_generation;
    m_pending.clear();
    m_busy = false;
    m_inPause = false;
    m_pauseTimer.stop();
}

void CommandQueue::setPaused(bool paused)
{
    if (paused == m_paused)
        return;
    m_paused = paused;

    if (m_inPause) {
        if (paused) {
            m_pauseRemainingMs -= static_cast<double>(m_pauseClock.elapsed()) * m_speed;
            m_pauseTimer.stop();
        } else {
            startPauseTimer();
        }
    }
    if (!paused)
        startNext();
}

void CommandQueue::setSpeed(double factor)
{
    if (m_inPause && !m_paused) {
        m_pauseRemainingMs -= static_cast<double>(m_pauseClock.elapsed()) * m_speed;
        m_speed = factor;
        startPauseTimer();
        return;
    }
    m_speed = factor;
}

void CommandQueue::startNext()
{
    if (m_busy || m_paused)
        return;
    if (m_pending.empty()) {
        emit idle();
        return;
    }

    const QJsonObject command = m_pending.front();
    m_pending.pop_front();
    m_busy = true;

    const QString type = command.value("tipo").toString();
    if (type == "fala") {
        // Não bloqueia: a legenda aparece e o próximo comando já começa
        emit speech(command.value("texto").toString());
        finishLater();
    } else if (type == "pausa") {
        m_inPause = true;
        m_pauseRemainingMs = std::max(0.0, command.value("segundos").toDouble()) * 1000.0;
        startPauseTimer();
    } else if (type == "forma" || type == "escrever" || type == "conectar" || type == "destacar"
               || type == "apagar" || type == "limpar") {
        m_scene.execute(command);
    } else {
        qWarning().noquote() << QString("Comando \"%1\" ainda não suportado nesta etapa; ignorado").arg(type);
        finishLater();
    }
}

void CommandQueue::commandFinished()
{
    if (!m_busy)
        return;
    m_busy = false;
    // Próximo comando na volta seguinte do event loop (sem recursão)
    QTimer::singleShot(0, this, [this, generation = m_generation] {
        if (generation == m_generation)
            startNext();
    });
}

void CommandQueue::finishLater()
{
    QTimer::singleShot(0, this, [this, generation = m_generation] {
        if (generation == m_generation)
            commandFinished();
    });
}

void CommandQueue::startPauseTimer()
{
    m_pauseClock.start();
    m_pauseTimer.start(static_cast<int>(std::max(0.0, m_pauseRemainingMs) / m_speed));
}
