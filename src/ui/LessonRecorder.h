#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointF>

#include <vector>

// Parâmetros da gravação de traços livres
struct LessonRecorderParams {
    double tolerance = 0.2;      // simplificação Douglas-Peucker, em unidades da lousa
    double pixelsPerUnit = 12.0; // conversão px da lousa → unidades
};

// Grava o que for desenhado com o mouse ou a caneta e transforma cada traço em
// um comando "traco_livre" do protocolo, com os pontos em unidades da lousa,
// simplificados para o arquivo não ficar gigante. O tempo e a pressão de cada
// ponto guardado são os originais, para a mão refazer o traço igualzinho.
class LessonRecorder : public QObject
{
    Q_OBJECT

public:
    explicit LessonRecorder(QObject *parent = nullptr);

    bool isRecording() const { return m_recording; }
    void setRecording(bool on);
    void setPixelsPerUnit(double value) { m_params.pixelsPerUnit = value; }
    int strokeCount() const { return m_count; }

public slots:
    // Ligados aos sinais da lousa (só o giz; o apagador não é gravado)
    void beginStroke();
    void addSample(const QPointF &boardPixels, float pressure, double timeMs);
    void endStroke();

signals:
    void commandRecorded(const QJsonObject &command);
    void recordingChanged(bool recording);

private:
    LessonRecorderParams m_params;
    std::vector<QPointF> m_points;   // unidades da lousa
    std::vector<float> m_pressure;
    std::vector<double> m_time;      // ms desde o início do traço
    double m_start = 0.0;
    bool m_recording = false;
    bool m_inStroke = false;
    int m_count = 0;
};
