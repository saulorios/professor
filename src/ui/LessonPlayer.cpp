#include "LessonPlayer.h"

#include "physics/Board.h"

#include <QFile>
#include <QJsonDocument>

#include <cmath>
#include <QJsonParseError>
#include <QSaveFile>

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
    // A lousa cresce para baixo conforme a aula precisa, e a vista acompanha
    connect(&m_scene, &Scene::canvasHeightChanged, this, [this](double units) {
        const double ppu = m_hand.params().pixelsPerUnit;
        if (m_board.ensureHeight(int(std::ceil(units * ppu))))
            emit canvasResized();
    });
    connect(&m_scene, &Scene::ensureVisible, this, [this](const QRectF &area) {
        const double ppu = m_hand.params().pixelsPerUnit;
        emit viewportRequested(QRectF(area.topLeft() * ppu, area.size() * ppu));
    });
    connect(&m_queue, &CommandQueue::speech, this, &LessonPlayer::speech);
    connect(&m_queue, &CommandQueue::stepFinished, this, &LessonPlayer::stepFinished);
    connect(&m_queue, &CommandQueue::question, this, &LessonPlayer::question);
    connect(&m_hand, &VirtualHand::boardChanged, this, &LessonPlayer::boardChanged);
    connect(&m_hand, &VirtualHand::chalkMoved, this, &LessonPlayer::chalkMoved);
    connect(&m_hand, &VirtualHand::chalkHidden, this, &LessonPlayer::chalkHidden);
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

bool LessonPlayer::save(const QString &path, QString *error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QString("Não foi possível gravar \"%1\": %2").arg(path, file.errorString());
        return false;
    }
    file.write(lessonText().toUtf8());
    file.write("\n");
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

QString LessonPlayer::lessonText() const
{
    QStringList lines;
    for (const QJsonObject &command : m_commands)
        lines << QString::fromUtf8(QJsonDocument(command).toJson(QJsonDocument::Compact));
    return lines.join('\n');
}

bool LessonPlayer::applyText(const QString &text, QStringList *errors)
{
    // Valida tudo antes de trocar a aula: um erro de sintaxe não pode derrubar
    // o que já está tocando
    QList<QJsonObject> parsed;
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty())
            continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError)
            errors->append(QString("linha %1: %2").arg(i + 1).arg(parseError.errorString()));
        else if (!document.isObject())
            errors->append(QString("linha %1: não é um objeto JSON").arg(i + 1));
        else if (!document.object().value("tipo").isString())
            errors->append(QString("linha %1: falta o campo \"tipo\"").arg(i + 1));
        else
            parsed.append(document.object());
    }
    if (!errors->isEmpty())
        return false;

    m_streaming = false;
    m_commands = parsed;
    m_loaded = true;
    startFromBeginning();
    return true;
}

void LessonPlayer::appendCommand(const QJsonObject &command)
{
    m_commands.append(command);
    m_loaded = true;
    emit commandReceived(command);
    emit stateChanged();
}

double LessonPlayer::startAnswer()
{
    return m_scene.startFreshScreen() * m_hand.params().pixelsPerUnit;
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
    if (clearBoard)
        emit lessonRestarted();
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
    // Interrompe tudo e volta à lousa limpa, com uma tela só
    m_queue.clear();
    m_board.reset();
    m_scene.reset();
    emit canvasResized();
    emit lessonRestarted();
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
