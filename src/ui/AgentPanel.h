#pragma once

#include <QFrame>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;

// Parâmetros ajustáveis do painel do professor
struct AgentPanelParams {
    int width = 320;
    int headerHeight = 35;   // mesma altura da TitleBar
    int margin = 10;
    int spacing = 6;
    int inputLines = 4;      // o campo cresce até isto
    int blinkMs = 420;       // ritmo do indicador de "desenhando"
};

// Um item da timeline: a pergunta e o que foi desenhado por ela
class TimelineCard : public QFrame
{
    Q_OBJECT

public:
    enum class State { Drawing, Done, Stopped, Failed };

    TimelineCard(const QString &question, QWidget *parent = nullptr);

    void setSummary(int commands, double seconds);
    void setState(State state);
    void setCurrent(bool current);
    void tick(); // anima o indicador de "desenhando"

    // Faixa do canvas (pixels) que esta resposta ocupou
    QRectF range;
    bool drew = false;   // já recebeu algum desenho
    QString question;

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void refresh();

    QLabel *m_question = nullptr;
    QLabel *m_summary = nullptr;
    State m_state = State::Drawing;
    int m_commands = 0;
    double m_seconds = 0.0;
    int m_phase = 0;
};

// Painel lateral do professor: cabeçalho, timeline das perguntas e o campo de
// pergunta no rodapé. É por aqui que se conversa com a IA; a barra de baixo da
// janela fica só com os controles de reprodução.
class AgentPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AgentPanel(QWidget *parent = nullptr);

    int preferredWidth() const { return m_params.width; }
    bool isEmpty() const { return m_cards.empty(); }

    // Timeline
    void beginSegment(const QString &question, double topPixels);
    void extendSegment(const QRectF &canvasPixels);
    void countCommand();
    void finishSegment(TimelineCard::State state);
    void clearTimeline();
    // Destaca o cartão do trecho que está à vista
    void setVisibleRange(double topPixels, double heightPixels);

public slots:
    void setBusy(bool busy);
    void setStepPending(bool pending);
    void setStatus(const QString &text);

signals:
    void asked(const QString &question);
    void stopRequested();
    void continueRequested();
    void logToggled(bool visible);
    void newLessonRequested();
    void historyRequested();
    void closeRequested();
    void goTo(const QRectF &canvasPixels);

private:
    void submit();
    void updateInputHeight();

    AgentPanelParams m_params;
    QVBoxLayout *m_timeline = nullptr;
    QWidget *m_timelineContent = nullptr;
    std::vector<TimelineCard *> m_cards;
    TimelineCard *m_current = nullptr;   // o que está sendo desenhado
    int m_currentCommands = 0;
    QTimer *m_blink = nullptr;
    double m_startedAt = 0.0;

    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_continue = nullptr;
    QPushButton *m_log = nullptr;
    QLabel *m_status = nullptr;
};
