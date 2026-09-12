#include "LessonPlayer.h"

#include "physics/Board.h"

#include <QFile>

LessonPlayer::LessonPlayer(Board &board, QObject *parent)
    : QObject(parent)
    , m_board(board)
    , m_hand(board)
    , m_scene(m_hand)
    , m_queue(m_scene)
{
    connect(&m_parser, &CommandParser::commandParsed, this, [this](const QJsonObject &command) {
        m_commands.append(command);
        emit commandReceived(command);
        if (!m_streaming)
            return;
        // Chegou uma linha inteira: já vai para a fila, sem esperar o resto
        m_queue.enqueue(command);
        m_playing = true;
        m_finished = false;
        emit stateChanged();
    });
    connect(&m_queue, &CommandQueue::speech, this, &LessonPlayer::speech);
    connect(&m_queue, &CommandQueue::stepFinished, this, &LessonPlayer::stepFinished);
    connect(&m_hand, &VirtualHand::boardChanged, this, &LessonPlayer::boardChanged);
    connect(&m_queue, &CommandQueue::idle, this, [this] {
        m_playing = false;
        m_finished = !m_streaming; // se ainda está chegando, a aula não acabou
        emit stateChanged();
    });
}

bool LessonPlayer::open(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QString("Não foi possível abrir \"%1\": %2").arg(path, file.errorString());
        return false;
    }

    m_streaming = false;
    m_commands.clear();
    m_parser.reset();
    m_parser.appendData(file.readAll());
    m_parser.finish();

    m_loaded = true;
    startFromBeginning();
    return true;
}

void LessonPlayer::startStream(bool clearBoard)
{
    m_parser.reset();
    if (clearBoard) {
        m_queue.clear();
        m_scene.reset();
        m_board.clear();
        m_commands.clear();
        emit boardChanged();
        emit speech(QString());
    }
    m_streaming = true;
    m_loaded = true;
    m_finished = false;
    m_hand.setPaused(false);
    m_queue.setPaused(false);
    emit stateChanged();
}

void LessonPlayer::appendStreamData(const QByteArray &data)
{
    m_parser.appendData(data);
}

void LessonPlayer::finishStream()
{
    m_parser.finish(); // a última linha pode vir sem '\n'
    m_streaming = false;
    if (m_queue.isIdle() && !m_hand.isBusy()) {
        m_playing = false;
        m_finished = true;
    }
    emit stateChanged();
}

void LessonPlayer::play()
{
    if (!m_loaded)
        return;
    if (m_finished) {
        startFromBeginning();
        return;
    }
    m_playing = true;
    m_hand.setPaused(false);
    m_queue.setPaused(false);
    emit stateChanged();
}

void LessonPlayer::pause()
{
    m_playing = false;
    m_queue.setPaused(true);
    m_hand.setPaused(true);
    emit stateChanged();
}

void LessonPlayer::restart()
{
    if (m_loaded)
        startFromBeginning();
}

void LessonPlayer::setSpeed(double factor)
{
    m_hand.setSpeed(factor);
    m_queue.setSpeed(factor);
}

void LessonPlayer::startFromBeginning()
{
    // Reiniciar redesenha o que já chegou; o resto continua entrando na fila
    // Interrompe tudo e volta à lousa limpa
    m_queue.clear();
    m_scene.reset();
    m_board.clear();
    emit boardChanged();
    emit speech(QString());

    m_finished = m_commands.isEmpty() && !m_streaming;
    m_playing = !m_commands.isEmpty();
    m_hand.setPaused(false);
    // Enfileira antes de soltar a pausa, para a fila não se declarar vazia
    for (const QJsonObject &command : m_commands)
        m_queue.enqueue(command);
    m_queue.setPaused(false);
    emit stateChanged();
}
