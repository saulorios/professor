#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QUrl>

// Parâmetros do lançamento automático do proxy
struct ProxyLauncherParams {
    int probeTimeoutMs = 1500;      // um proxy que já está rodando responde nesse tempo
    int startupTimeoutMs = 30000;   // o uvicorn tem até isto para responder ao /saude
    int pollIntervalMs = 500;       // intervalo entre as consultas ao /saude na subida
    int stopTimeoutMs = 3000;       // espera educada ao encerrar, antes de matar
    int searchDepth = 5;            // quantas pastas acima do executável procurar proxy/
};

// Sobe o proxy Python (proxy/servidor.py, com uvicorn) junto com o aplicativo e
// o encerra ao sair. Só age quando o endereço da IA é local: se já houver um
// proxy respondendo nessa porta (rodado à mão no terminal), usa esse e não o
// encerra depois. A chave continua só no proxy: aqui nada lê o .env.
//
// Onde procura: a pasta da variável LOUSA_PROXY_DIR ou a pasta proxy/ na pasta
// do executável e acima dela (build/ fica ao lado de proxy/). O Python é o do
// .venv da pasta, se existir; senão python3/python do sistema.
// LOUSA_PROXY_AUTO=0 desliga o lançamento.
class ProxyLauncher : public QObject
{
    Q_OBJECT

public:
    explicit ProxyLauncher(QObject *parent = nullptr);
    ~ProxyLauncher() override;

    // `aulaUrl`: endereço usado pelo AiClient (ex.: http://127.0.0.1:8000/aula)
    void start(const QUrl &aulaUrl);
    void stop();

    bool ownsProcess() const { return m_process.state() != QProcess::NotRunning; }

    static QString findProxyDir(const QString &startDir, int depth);
    static QString findPython(const QString &proxyDir);

signals:
    void statusChanged(const QString &text);   // mensagem curta para a interface
    void output(const QString &line);          // saída do uvicorn, linha a linha
    void ready(const QString &summary);        // /saude respondeu
    void failed(const QString &message);

private:
    enum class Phase { Idle, Probing, Starting, Running, Failed };

    void probe();
    void launch();
    void poll();
    void healthReply(QNetworkReply *reply);
    void readOutput();
    void processEnded(int exitCode, QProcess::ExitStatus status);
    void fail(const QString &message);

    ProxyLauncherParams m_params;
    QNetworkAccessManager m_network;
    QProcess m_process;
    QTimer m_poll;
    QTimer m_deadline;
    QUrl m_health;
    Phase m_phase = Phase::Idle;
    QString m_lastOutput;          // últimas linhas, para explicar uma falha
};
