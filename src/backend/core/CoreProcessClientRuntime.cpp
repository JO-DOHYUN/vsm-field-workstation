#include "core/CoreProcessClientRuntime.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace CanMonitorCore {

CoreProcessClientRuntime::CoreProcessClientRuntime(QObject* parent)
    : QObject(parent) {
    connectClientSignals();
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CoreProcessClientRuntime::readStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &CoreProcessClientRuntime::readStandardError);
    connect(&m_process,
            &QProcess::errorOccurred,
            this,
            [this](QProcess::ProcessError) {
                m_lastMessage = m_process.errorString();
                emit errorOccurred(QStringLiteral("core process error: %1").arg(m_lastMessage));
                emit stateChanged(isActive(), m_lastMessage);
            });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus status) {
                m_client.disconnectFromServer();
                m_lastMessage = QStringLiteral("core process exited: code %1 status %2")
                                    .arg(exitCode)
                                    .arg(status == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash"));
                emit stateChanged(false, m_lastMessage);
            });
}

CoreProcessClientRuntime::~CoreProcessClientRuntime() {
    stop();
}

bool CoreProcessClientRuntime::startSerial(const QString& executablePath, const QString& portName, QString* errorOut) {
    const QString endpoint = portName.trimmed();
    if (endpoint.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty core process serial port");
        return false;
    }
    return startProcess(executablePath, {QStringLiteral("--port"), endpoint}, errorOut);
}

bool CoreProcessClientRuntime::startServerOnly(const QString& executablePath, QString* errorOut) {
    return startProcess(executablePath, {}, errorOut);
}

void CoreProcessClientRuntime::stop() {
    m_client.disconnectFromServer();
    if (m_process.state() == QProcess::NotRunning) return;
    m_process.terminate();
    if (!m_process.waitForFinished(1500)) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

bool CoreProcessClientRuntime::isActive() const {
    return m_process.state() != QProcess::NotRunning;
}

bool CoreProcessClientRuntime::isIpcConnected() const {
    return m_client.isConnected();
}

QJsonObject CoreProcessClientRuntime::statusJson() const {
    return QJsonObject{{QStringLiteral("core_process_active"), isActive()},
                       {QStringLiteral("core_process_ipc_connected"), m_client.isConnected()},
                       {QStringLiteral("core_process_server_name"), m_serverName},
                       {QStringLiteral("core_process_message"), m_lastMessage}};
}

bool CoreProcessClientRuntime::requestView(const CoreViewClientRuntime::ViewRequest& request) {
    if (!request.valid) return false;
    return m_client.requestViewWithId(request.requestId, request.viewName, request.sinceSeq, request.limit);
}

bool CoreProcessClientRuntime::startProcess(const QString& executablePath, const QStringList& extraArgs, QString* errorOut) {
    if (m_process.state() != QProcess::NotRunning) stop();

    const QFileInfo exe(executablePath);
    if (!exe.exists()) {
        if (errorOut) *errorOut = QStringLiteral("core process executable not found: %1").arg(executablePath);
        return false;
    }

    m_serverName = makeServerName();
    m_stdoutBuffer.clear();
    m_startupSeen = false;
    m_lastMessage = QStringLiteral("starting core process");

    QStringList args{QStringLiteral("--server"), m_serverName};
    args += extraArgs;
    m_process.setProgram(exe.absoluteFilePath());
    m_process.setArguments(args);
    m_process.setWorkingDirectory(exe.absolutePath());
    m_process.start();
    if (!m_process.waitForStarted(3000)) {
        if (errorOut) *errorOut = QStringLiteral("failed to start core process: %1").arg(m_process.errorString());
        return false;
    }

    emit stateChanged(true, m_lastMessage);
    QTimer::singleShot(150, this, &CoreProcessClientRuntime::connectIpc);
    if (errorOut) errorOut->clear();
    return true;
}

void CoreProcessClientRuntime::connectClientSignals() {
    connect(&m_client, &CoreIpcClientRuntime::connectedChanged, this, [this](bool connected) {
        m_lastMessage = connected ? QStringLiteral("core IPC connected") : QStringLiteral("core IPC disconnected");
        emit stateChanged(isActive(), m_lastMessage);
    });
    connect(&m_client, &CoreIpcClientRuntime::viewChanged, this, &CoreProcessClientRuntime::viewChanged);
    connect(&m_client,
            &CoreIpcClientRuntime::viewSnapshotReceived,
            this,
            &CoreProcessClientRuntime::viewSnapshotReady);
    connect(&m_client, &CoreIpcClientRuntime::errorReceived, this, [this](quint64, const QString& error, const QString& detail) {
        const QString message = detail.isEmpty() ? error : QStringLiteral("%1: %2").arg(error, detail);
        emit errorOccurred(QStringLiteral("core IPC error: %1").arg(message));
        if (isActive() && !m_client.isConnected()) {
            QTimer::singleShot(250, this, &CoreProcessClientRuntime::connectIpc);
        }
    });
}

void CoreProcessClientRuntime::readStandardOutput() {
    m_stdoutBuffer += m_process.readAllStandardOutput();
    for (;;) {
        const qsizetype newline = m_stdoutBuffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) continue;
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("ok")).toBool(false)) {
            m_startupSeen = true;
            m_lastMessage = QStringLiteral("core process ready");
            emit stateChanged(true, m_lastMessage);
            connectIpc();
        }
    }
}

void CoreProcessClientRuntime::readStandardError() {
    const QString message = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    if (message.isEmpty()) return;
    emit errorOccurred(QStringLiteral("core process stderr: %1").arg(message));
}

void CoreProcessClientRuntime::connectIpc() {
    if (!isActive() || m_client.isConnected() || m_serverName.isEmpty()) return;
    m_client.connectToServer(m_serverName);
    if (!m_startupSeen) {
        QTimer::singleShot(250, this, &CoreProcessClientRuntime::connectIpc);
    }
}

QString CoreProcessClientRuntime::makeServerName() {
    return QStringLiteral("vsm-core-ui-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
}

} // namespace CanMonitorCore
