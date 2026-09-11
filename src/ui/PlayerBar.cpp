#include "PlayerBar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace {

QPushButton *makeButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

PlayerBar::PlayerBar(QWidget *parent)
    : QWidget(parent)
{
    // Necessário para que o QSS (fundo e borda) seja aplicado a uma subclasse de QWidget
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(m_params.height + m_params.separatorHeight);

    m_play = makeButton("Play", this);
    m_pause = makeButton("Pausar", this);
    m_restart = makeButton("Reiniciar", this);
    connect(m_play, &QPushButton::clicked, this, &PlayerBar::playClicked);
    connect(m_pause, &QPushButton::clicked, this, &PlayerBar::pauseClicked);
    connect(m_restart, &QPushButton::clicked, this, &PlayerBar::restartClicked);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(m_params.margin, m_params.separatorHeight, m_params.margin, 0);
    layout->setSpacing(m_params.spacing);
    layout->addWidget(m_play);
    layout->addWidget(m_pause);
    layout->addWidget(m_restart);
    layout->addStretch(1);
    layout->addWidget(new QLabel("Velocidade", this));

    // Velocidades exclusivas entre si; 1x começa marcado
    m_speeds = new QButtonGroup(this);
    const double factors[] = {0.5, 1.0, 2.0, 4.0};
    for (double factor : factors) {
        QPushButton *button = makeButton(QString("%1x").arg(factor), this);
        button->setObjectName("SpeedButton");
        button->setCheckable(true);
        button->setChecked(factor == 1.0);
        m_speeds->addButton(button);
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, factor] { emit speedChanged(factor); });
    }

    setState(false, false);
}

void PlayerBar::setState(bool loaded, bool playing)
{
    m_play->setEnabled(loaded && !playing);
    m_pause->setEnabled(loaded && playing);
    m_restart->setEnabled(loaded);
}
