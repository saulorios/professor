#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;

// Parâmetros do editor de aula
struct LessonEditorParams {
    int height = 260;
    int margin = 8;
    int spacing = 6;
};

// Editor da aula em JSON Lines (F9): o usuário desenha, grava e depois acrescenta
// falas e pausas no texto. "Aplicar e reproduzir" (Ctrl+Enter) valida antes de
// rodar; os erros aparecem com o número da linha, sem travar nada.
class LessonEditor : public QWidget
{
    Q_OBJECT

public:
    explicit LessonEditor(QWidget *parent = nullptr);

    QString text() const;
    void setText(const QString &text);
    void setErrors(const QStringList &errors);

signals:
    void applyRequested();
    void saveRequested();
    void openRequested();

private:
    void insertTemplate(const QString &line);

    LessonEditorParams m_params;
    QPlainTextEdit *m_text = nullptr;
    QLabel *m_status = nullptr;
};
