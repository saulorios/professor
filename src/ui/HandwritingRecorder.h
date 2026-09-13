#pragma once

#include "handwriting/data/GlyphDatabase.h"

#include <QStringList>
#include <QWidget>

class HandwritingCapture;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

// Parâmetros da janela do gravador
struct HandwritingRecorderParams {
    QSize initialSize{1100, 680};
    int sideWidth = 330;
    // Letras do primeiro lote: curvas, diagonais, retas, laços, strokes múltiplos
    QStringList quickCharacters{"A", "E", "I", "O", "U", "M", "N", "R", "S"};
    int timestampsShown = 6;    // primeiros timestamps listados por stroke
};

// Gravador de escrita manual (F7): escolhe o caractere, escreve a letra com
// mouse, caneta ou toque, desfaz/limpa e salva como variante nova (A01, A02...).
// A lista mostra as variantes do caractere; clicar numa carrega e mostra os
// strokes, a ordem e as medidas; "Excluir variante" apaga a selecionada
// (com confirmação). O banco fica em <pasta do executável>/handwriting
// ou na pasta da variável LOUSA_ESCRITA.
class HandwritingRecorder : public QWidget
{
    Q_OBJECT

public:
    explicit HandwritingRecorder(QWidget *parent = nullptr);

    static QString defaultDatabasePath();

signals:
    // Uma variante foi salva, excluída ou o banco foi recarregado do disco
    void databaseChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setCharacter(const QString &character);
    void reloadDatabase();
    void refreshVariants();
    void refreshDetails();
    void saveVariant();
    void loadSelectedVariant();
    void deleteSelectedVariant();
    void updateButtons();
    QString currentCharacter() const;
    QString describe(const handwriting::GlyphVariant &variant) const;

    HandwritingRecorderParams m_params;
    handwriting::GlyphDatabase m_database;
    HandwritingCapture *m_capture = nullptr;
    QLineEdit *m_character = nullptr;
    QLabel *m_next = nullptr;
    QListWidget *m_variants = nullptr;
    QPlainTextEdit *m_details = nullptr;
    QCheckBox *m_showTrajectory = nullptr;
    QCheckBox *m_showPoints = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_delete = nullptr;
    bool m_previousCompression = true;   // estado do atributo antes de abrir a janela
};
