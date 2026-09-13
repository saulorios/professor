#include "ProxyLauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QStandardPaths>

#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#endif

namespace {

constexpr int kOutputLinesKept = 8;

bool isLocalHost(const QString &host)
{
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

} // namespace

ProxyLauncher::ProxyLauncher(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(m_params.pollIntervalMs);
    m_deadline.setSingleShot(true);
    m_process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_LINUX
    // Se o app morrer sem passar pelo destrutor (travou, kill -9), o kernel
    // encerra o proxy junto, em vez de deixá-lo órfão segurando a porta
    m_process.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
#endif

    connect(&m_poll, &QTimer::timeout, this, &ProxyLauncher::poll);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        fail(QString("o proxy não respondeu em %1 s").arg(m_params.startupTimeoutMs / 1000));
    });
    connect(&m_process, &QProcess::readyRead, this, &ProxyLauncher::readOutput);
    connect(&m_process, &QProcess::finished, this, &ProxyLauncher::processEnded);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(QString("não foi possível executar %1: %2").arg(m_process.program(), m_process.errorString()));
    });
}

ProxyLauncher::~ProxyLauncher()
{
    stop();
}

QString ProxyLauncher::findProxyDir(const QString &startDir, int depth)
{
    const QString fromEnv = qEnvironmentVariable("LOUSA_PROXY_DIR");
    if (!fromEnv.isEmpty())
        return QFileInfo::exists(QDir(fromEnv).filePath("servidor.py")) ? QDir(fromEnv).absolutePath() : QString();

    QDir dir(startDir);
    for (int level = 0; level <= depth; ++level) {
        if (QFileInfo::exists(dir.filePath("proxy/servidor.py")))
            return QDir(dir.filePath("proxy")).absolutePath();
        if (!dir.cdUp())
            break;
    }
    return QString();
}

QString ProxyLauncher::findPython(const QString &proxyDir)
{
    const QDir dir(proxyDir);
    for (const QString &candidate : {QString(".venv/bin/python"), QString(".venv/Scripts/python.exe")})
        if (QFileInfo(dir.filePath(candidate)).isExecutable())
            return dir.filePath(candidate);
    for (const QString &name : {QString("python3"), QString("python")}) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty())
            return path;
    }
    return QString();
}

void ProxyLauncher::start(const QUrl &aulaUrl)
{
    if (m_phase != Phase::Idle && m_phase != Phase::Failed)
        return;
    if (qEnvironmentVariable("LOUSA_PROXY_AUTO") == "0") {
        emit statusChanged("Proxy: início automático desligado (LOUSA_PROXY_AUTO=0)");
        return;
    }
    if (!isLocalHost(aulaUrl.host())) {
        emit statusChanged(QString("Proxy remoto em %1: não é iniciado aqui").arg(aulaUrl.host()));
        return;
    }
    m_health = aulaUrl;
    m_health.setPath("/saude");
    m_phase = Phase::Probing;
    probe();
}

void ProxyLauncher::probe()
{
    QNetworkRequest request(m_health);
    request.setTransferTimeout(m_phase == Phase::Probing ? m_params.probeTimeoutMs : m_params.pollIntervalMs * 2);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { healthReply(reply); });
}

void ProxyLauncher::poll()
{
    if (m_phase == Phase::Starting)
        probe();
}

void ProxyLauncher::healthReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_phase != Phase::Probing && m_phase != Phase::Starting)
        return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status == 200) {
        const QJsonObject info = QJsonDocument::fromJson(reply->readAll()).object();
        QString summary = QString("%1 · %2").arg(info.value("provedor").toString(), info.value("modelo").toString());
        if (!info.value("chave").toBool(true))
            summary += " · sem chave configurada";
        const bool ours = m_phase == Phase::Starting;
        m_phase = Phase::Running;
        m_poll.stop();
        m_deadline.stop();
        emit statusChanged(QString("Proxy %1 (%2)").arg(ours ? "iniciado" : "já estava rodando", summary));
        emit ready(summary);
        return;
    }
    if (status != 0) {
        // Alguém respondeu, mas não é o proxy: não dá para subir outro na mesma porta
        fail(QString("a porta %1 está ocupada por outro serviço (HTTP %2 em /saude)").arg(m_health.port()).arg(status));
        return;
    }
    if (m_phase == Phase::Probing)
        launch();
}

void ProxyLauncher::launch()
{
    const QString proxyDir = findProxyDir(QCoreApplication::applicationDirPath(), m_params.searchDepth);
    if (proxyDir.isEmpty()) {
        fail("pasta proxy/ não encontrada (defina LOUSA_PROXY_DIR)");
        return;
    }
    const QString python = findPython(proxyDir);
    if (python.isEmpty()) {
        fail("Python não encontrado (crie o .venv da pasta proxy/)");
        return;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");   // a saída chega ao log na hora
    m_process.setProcessEnvironment(env);
    m_process.setWorkingDirectory(proxyDir);
    m_lastOutput.clear();
    m_phase = Phase::Starting;
    emit statusChanged("Iniciando o proxy...");
    emit output(QString("iniciando %1 -m uvicorn servidor:app (%2)").arg(python, proxyDir));

    m_process.start(python, {"-m", "uvicorn", "servidor:app", "--host", m_health.host(), "--port",
                             QString::number(m_health.port(80))});
    m_poll.start();
    m_deadline.start(m_params.startupTimeoutMs);
}

void ProxyLauncher::readOutput()
{
    const QStringList lines = QString::fromUtf8(m_process.readAll()).split('\n', Qt::SkipEmptyParts);
    QStringList kept = m_lastOutput.split('\n', Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        emit output(line);
        kept << line;
    }
    while (kept.size() > kOutputLinesKept)
        kept.removeFirst();
    m_lastOutput = kept.join('\n');
}

void ProxyLauncher::processEnded(int exitCode, QProcess::ExitStatus)
{
    if (m_phase != Phase::Starting && m_phase != Phase::Running)
        return;
    readOutput();
    const QString last = m_lastOutput.section('\n', -1);
    fail(QString("o proxy encerrou (código %1)%2").arg(exitCode).arg(last.isEmpty() ? QString() : ": " + last));
}

void ProxyLauncher::fail(const QString &message)
{
    if (m_phase == Phase::Failed)
        return;
    m_phase = Phase::Failed;
    m_poll.stop();
    m_deadline.stop();
    // Com a fase já em Failed, o fim forçado não vira outra falha em processEnded()
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(m_params.stopTimeoutMs);
    }
    emit statusChanged("Proxy: " + message);
    emit failed(message);
}

void ProxyLauncher::stop()
{
    m_phase = Phase::Idle;
    m_poll.stop();
    m_deadline.stop();
    if (m_process.state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_WIN
    m_process.kill();   // no Windows terminate() não chega a processos de console
#else
    m_process.terminate();
    if (m_process.waitForFinished(m_params.stopTimeoutMs))
        return;
    m_process.kill();
#endif
    m_process.waitForFinished(m_params.stopTimeoutMs);
}
