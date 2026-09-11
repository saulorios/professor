#pragma once

#include "hand/VirtualHand.h"
#include "protocol/CommandParser.h"
#include "protocol/CommandQueue.h"
#include "scene/Scene.h"

#include <QJsonObject>
#include <QList>
#include <QObject>

class Board;

// Reproduz uma aula a partir de um arquivo .jsonl (sem IA e sem rede):
// arquivo → CommandParser → CommandQueue → Scene → VirtualHand → física.
class LessonPlayer : public QObject
{
    Q_OBJECT

public:
    explicit LessonPlayer(Board &board, QObject *parent = nullptr);

    // Carrega a aula e começa a reproduzir; em erro de leitura devolve false e a mensagem
    bool open(const QString &path, QString *error);

    void play();
    void pause();
    void restart();
    void setSpeed(double factor);

    // Parâmetros da mão (valem a partir do próximo comando desenhado)
    void setHandParams(const HandParams &params) { m_hand.setParams(params); }
    const HandParams &handParams() const { return m_hand.params(); }

    bool isLoaded() const { return m_loaded; }
    bool isPlaying() const { return m_playing; }

signals:
    void speech(const QString &text);  // legenda atual ("" apaga)
    void boardChanged();               // a mão alterou o depósito de giz
    void stateChanged();

private:
    void startFromBeginning();

    Board &m_board;
    VirtualHand m_hand;
    Scene m_scene;
    CommandQueue m_queue;
    CommandParser m_parser;

    QList<QJsonObject> m_commands;     // aula inteira, para reiniciar
    bool m_loaded = false;
    bool m_playing = false;
    bool m_finished = false;
};
