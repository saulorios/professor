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
    });
    connect(&m_queue, &CommandQueue::speech, this, &LessonPlayer::speech);
    connect(&m_hand, &VirtualHand::boardChanged, this, &LessonPlayer::boardChanged);
    connect(&m_queue, &CommandQueue::idle, this, [this] {
        m_playing = false;
        m_finished = true;
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

    m_commands.clear();
    m_parser.reset();
    m_parser.appendData(file.readAll());
    m_parser.finish();

    m_loaded = true;
    startFromBeginning();
    return true;
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
    // Interrompe tudo e volta à lousa limpa
    m_queue.clear();
    m_scene.reset();
    m_board.clear();
    emit boardChanged();
    emit speech(QString());

    m_finished = m_commands.isEmpty();
    m_playing = !m_finished;
    m_hand.setPaused(false);
    // Enfileira antes de soltar a pausa, para a fila não se declarar vazia
    for (const QJsonObject &command : m_commands)
        m_queue.enqueue(command);
    m_queue.setPaused(false);
    emit stateChanged();
}
