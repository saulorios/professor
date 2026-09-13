#pragma once

#include "hand/VirtualHand.h"
#include "protocol/CommandParser.h"
#include "protocol/CommandQueue.h"
#include "scene/Scene.h"

#include <QJsonObject>
#include <QList>
#include <QObject>

class Board;

// Reproduz uma aula, vinda de um arquivo .jsonl ou da IA:
// texto → CommandParser → CommandQueue → Scene → VirtualHand → física.
// No modo streaming cada comando é desenhado assim que a linha chega inteira.
class LessonPlayer : public QObject
{
    Q_OBJECT

public:
    explicit LessonPlayer(Board &board, QObject *parent = nullptr);

    // Carrega a aula e começa a reproduzir; em erro de leitura devolve false e a mensagem
    bool open(const QString &path, QString *error);
    bool save(const QString &path, QString *error) const;

    // A aula inteira em JSON Lines, para o editor
    QString lessonText() const;
    // Valida o texto e, se estiver todo certo, reproduz; senão devolve false e
    // a lista de erros ("linha N: ..."), sem mexer na aula atual
    bool applyText(const QString &text, QStringList *errors);
    // Acrescenta um comando à aula sem desenhá-lo (usado pelo gravador de traços)
    void appendCommand(const QJsonObject &command);
    // Prepara a lousa para uma resposta nova; devolve o topo dela em pixels
    double startAnswer();

    // Aula que chega aos poucos (da IA). `clearBoard` false continua a aula
    // atual, mantendo o que já está na lousa (resposta a "fim_passo").
    void startStream(bool clearBoard = true);
    void appendStreamData(const QByteArray &data);
    void finishStream();
    bool isStreaming() const { return m_streaming; }

    void play();
    void pause();
    void restart();
    void setSpeed(double factor);

    // Parâmetros da mão (valem a partir do próximo comando desenhado)
    void setHandParams(const HandParams &params) { m_hand.setParams(params); }
    const HandParams &handParams() const { return m_hand.params(); }

    // Humanização da escrita (vale a partir do próximo texto)
    void setHumanizerParams(const HumanizerParams &params) { m_scene.setHumanizerParams(params); }
    const HumanizerParams &humanizerParams() const { return m_scene.humanizerParams(); }

    // Cena da aula (elementos desenhados, área útil), para o modo de depuração
    const Scene &scene() const { return m_scene; }

    bool isLoaded() const { return m_loaded; }
    bool isPlaying() const { return m_playing; }

signals:
    void speech(const QString &text);  // legenda atual ("" apaga)
    void boardChanged();               // a mão alterou o depósito de giz
    void stateChanged();
    void commandReceived(const QJsonObject &command); // para o log da aula
    void stepFinished();               // a IA terminou um passo e espera
    void question(const QString &text); // "pergunta" de uma aula gravada
    // Linha {"tipo":"erro"} vinda do proxy no meio da resposta: não entra na aula
    void streamError(const QString &message);
    void chalkMoved(const ChalkPose &pose); // giz visível na tela
    void chalkHidden();
    void canvasResized();                          // a lousa cresceu (ou voltou a uma tela)
    void lessonRestarted();                        // aula nova: o motor reassume a vista
    void viewportRequested(const QRectF &canvasPixels); // o motor quer mostrar esta faixa

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
    bool m_streaming = false;          // a aula ainda está chegando pela rede
};
