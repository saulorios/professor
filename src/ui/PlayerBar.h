#pragma once

#include <QWidget>

class QButtonGroup;
class QPushButton;

// Parâmetros ajustáveis da barra do player
struct PlayerBarParams {
    int height = 30;            // altura útil
    int separatorHeight = 1;    // linha superior (border-top no QSS)
    int margin = 10;            // margens laterais
    int spacing = 4;            // espaço entre os botões
};

// Barra inferior fina: Play / Pausar / Reiniciar e velocidade (0.5x a 4x)
class PlayerBar : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerBar(QWidget *parent = nullptr);

public slots:
    // Habilita os botões conforme o estado da aula
    void setState(bool loaded, bool playing);

signals:
    void playClicked();
    void pauseClicked();
    void restartClicked();
    void speedChanged(double factor);

private:
    PlayerBarParams m_params;
    QPushButton *m_play = nullptr;
    QPushButton *m_pause = nullptr;
    QPushButton *m_restart = nullptr;
    QButtonGroup *m_speeds = nullptr;
};
