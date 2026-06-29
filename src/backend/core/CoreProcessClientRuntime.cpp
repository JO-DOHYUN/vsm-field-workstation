#include "core/CoreProcessClientRuntime.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>

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
                setLifecycleState(LifecycleState::Degraded, m_process.errorString());
                emit errorOccurred(QStringLiteral("core process error: %1").arg(m_lastMessage));
            });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus status) {
                m_connectRetryScheduled = false;
                m_client.disconnectFromServer();
                setLifecycleState(LifecycleState::Stopped,
                                  QStringLiteral("core process exited: code %1 status %2")
                                      .arg(exitCode)
                                      .arg(status == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash")));
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
    if (isActive() && m_client.isConnected()) {
        m_client.startTransport(QStringLiteral("serial"), endpoint);
        m_transportOpenPending = true;
        setLifecycleState(LifecycleState::TransportOpening, QStringLiteral("opening core serial transport: %1").arg(endpoint));
        scheduleStartupTimeout();
        if (errorOut) errorOut->clear();
        return true;
    }
    if (isActive()) {
        return queuePendingTransportStart(QStringLiteral("serial"), endpoint, errorOut);
    }
    return startProcess(executablePath, {QStringLiteral("--port"), endpoint}, errorOut);
}

bool CoreProcessClientRuntime::startGatewayTcp(const QString& executablePath, const QString& endpoint, QString* errorOut) {
    if (!m_runtimeProfile.transportPolicy().labGatewayEnabled) {
        if (errorOut) *errorOut = QStringLiteral("gateway tcp disabled by runtime profile: %1").arg(m_runtimeProfile.key());
        return false;
    }
    const QString normalized = endpoint.trimmed();
    if (!normalized.startsWith(QStringLiteral("tcp://"))) {
        if (errorOut) *errorOut = QStringLiteral("invalid core process gateway endpoint: %1").arg(endpoint);
        return false;
    }
    if (isActive() && m_client.isConnected()) {
        m_client.startTransport(QStringLiteral("gateway_tcp"), normalized);
        m_transportOpenPending = true;
        setLifecycleState(LifecycleState::TransportOpening, QStringLiteral("opening core gateway transport: %1").arg(normalized));
        scheduleStartupTimeout();
        if (errorOut) errorOut->clear();
        return true;
    }
    if (isActive()) {
        return queuePendingTransportStart(QStringLiteral("gateway_tcp"), normalized, errorOut);
    }
    return startProcess(executablePath, {QStringLiteral("--gateway"), normalized}, errorOut);
}

bool CoreProcessClientRuntime::startServerOnly(const QString& executablePath, QString* errorOut) {
    if (isActive()) {
        if (errorOut) errorOut->clear();
        return true;
    }
    return startProcess(executablePath, {}, errorOut);
}

bool CoreProcessClientRuntime::stopTransport(QString* errorOut) {
    if (!m_client.isConnected()) {
        if (errorOut) *errorOut = QStringLiteral("core IPC is not connected");
        return false;
    }
    m_client.stopTransport();
    if (errorOut) errorOut->clear();
    return true;
}

void CoreProcessClientRuntime::stop() {
    ++m_lifecycleGeneration;
    setLifecycleState(LifecycleState::Stopping, QStringLiteral("stopping core process"));
    m_connectRetryScheduled = false;
    m_client.disconnectFromServer();
    m_pendingTransportMode.clear();
    m_pendingTransportEndpoint.clear();
    m_transportOpenPending = false;
    if (m_process.state() == QProcess::NotRunning) {
        setLifecycleState(LifecycleState::Stopped, QStringLiteral("core process stopped"));
        return;
    }
    m_process.terminate();
    if (!m_process.waitForFinished(1500)) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    setLifecycleState(LifecycleState::Stopped, QStringLiteral("core process stopped"));
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
                       {QStringLiteral("core_process_state"), lifecycleStateText(m_lifecycleState)},
                       {QStringLiteral("core_process_state_since_ms"), QString::number(m_lifecycleStateChangedMs)},
                       {QStringLiteral("core_process_ipc_connect_attempts"), m_connectAttempts},
                       {QStringLiteral("core_process_startup_timeouts"), QString::number(m_startupTimeouts)},
                       {QStringLiteral("core_process_ipc_connect_timeouts"), QString::number(m_ipcConnectTimeouts)},
                       {QStringLiteral("core_process_transport_open_timeouts"), QString::number(m_transportOpenTimeouts)},
                       {QStringLiteral("core_process_transport_open_pending"), m_transportOpenPending},
                       {QStringLiteral("core_process_ipc_retry_scheduled"), m_connectRetryScheduled},
                       {QStringLiteral("runtime_profile"), m_runtimeProfile.toJson()},
                       {QStringLiteral("core_process_message"), m_lastMessage}};
}

bool CoreProcessClientRuntime::requestView(const CoreViewClientRuntime::ViewRequest& request) {
    if (!request.valid) return false;
    return m_client.requestViewWithId(request.requestId, request.viewName, request.sinceSeq, request.limit);
}

bool CoreProcessClientRuntime::sendHostFrame(const QByteArray& frame, const QString& summary, QString* errorOut) {
    if (!m_runtimeProfile.transportPolicy().hostTxEnabled) {
        if (errorOut) *errorOut = QStringLiteral("host tx disabled by runtime profile: %1").arg(m_runtimeProfile.key());
        return false;
    }
    if (frame.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty core process host frame");
        return false;
    }
    if (!m_client.isConnected()) {
        if (errorOut) *errorOut = QStringLiteral("core IPC is not connected");
        return false;
    }
    m_client.sendHostFrame(frame, summary);
    if (errorOut) errorOut->clear();
    return true;
}

bool CoreProcessClientRuntime::startControlCycle(int signedCommand,
                                                 int rpm,
                                                 double steeringDeg,
                                                 quint8 motorMode,
                                                 quint8 drivingMode,
                                                 quint8 bus,
                                                 int periodMs,
                                                 int frameGapMs,
                                                 QString* errorOut) {
    if (!m_runtimeProfile.transportPolicy().controlCycleEnabled) {
        if (errorOut) *errorOut = QStringLiteral("control cycle disabled by runtime profile: %1").arg(m_runtimeProfile.key());
        return false;
    }
    if (!m_client.isConnected()) {
        if (errorOut) *errorOut = QStringLiteral("core IPC is not connected");
        return false;
    }
    m_client.startControlCycle(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus, periodMs, frameGapMs);
    if (errorOut) errorOut->clear();
    return true;
}

bool CoreProcessClientRuntime::updateControlCycle(int signedCommand,
                                                  int rpm,
                                                  double steeringDeg,
                                                  quint8 motorMode,
                                                  quint8 drivingMode,
                                                  quint8 bus,
                                                  QString* errorOut) {
    if (!m_runtimeProfile.transportPolicy().controlCycleEnabled) {
        if (errorOut) *errorOut = QStringLiteral("control cycle disabled by runtime profile: %1").arg(m_runtimeProfile.key());
        return false;
    }
    if (!m_client.isConnected()) {
        if (errorOut) *errorOut = QStringLiteral("core IPC is not connected");
        return false;
    }
    m_client.updateControlCycle(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus);
    if (errorOut) errorOut->clear();
    return true;
}

bool CoreProcessClientRuntime::stopControlCycle(QString* errorOut) {
    if (!m_runtimeProfile.transportPolicy().controlCycleEnabled) {
        if (errorOut) errorOut->clear();
        return true;
    }
    if (!m_client.isConnected()) {
        if (errorOut) *errorOut = QStringLiteral("core IPC is not connected");
        return false;
    }
    m_client.stopControlCycle();
    if (errorOut) errorOut->clear();
    return true;
}

bool CoreProcessClientRuntime::sendControlCycleBurstOnce(int signedCommand,
                                                         int rpm,
                                                         double steeringDeg,
                                                         quint8 motorMode,
                                                         quint8 drivingMode,
                                                         quint8 bus,
                                                         const QString& reason,
                                                         bool resetSlew,
                                                         QString* errorOut) {
    if (!m_runtimeProfile.transportPolicy().controlCycleEnabled) {
        if (errorOut) *errorOut = QStringLiteral("control cycle disabled by runtime profile: %1").arg(m_runtimeProfile.key());
        return false;
    }
    if (!m_client.isConnected()) {
        if (errorOut) *errorOut = QStringLiteral("core IPC is not connected");
        return false;
    }
    m_client.sendControlCycleBurstOnce(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus, reason, resetSlew);
    if (errorOut) errorOut->clear();
    return true;
}

bool CoreProcessClientRuntime::startCapture(const QString& sessionDir, const QJsonObject& metadata, QString* errorOut) {
    if (sessionDir.trimmed().isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty core capture session directory");
        return false;
    }
    if (!m_client.isConnected()) {
        if (errorOut) *errorOut = QStringLiteral("core IPC is not connected");
        return false;
    }
    m_client.startCapture(sessionDir, metadata);
    if (errorOut) errorOut->clear();
    return true;
}

bool CoreProcessClientRuntime::stopCapture(const QString& inactivePath, const QJsonObject& diagnostics, QString* errorOut) {
    if (!m_client.isConnected()) {
        if (errorOut) *errorOut = QStringLiteral("core IPC is not connected");
        return false;
    }
    m_client.stopCapture(inactivePath, diagnostics);
    if (errorOut) errorOut->clear();
    return true;
}

bool CoreProcessClientRuntime::setAnalysisModel(const QString& modelPath, bool modelEnabled, QString* errorOut) {
    if (!m_client.isConnected()) {
        if (errorOut) *errorOut = QStringLiteral("core IPC is not connected");
        return false;
    }
    m_client.setAnalysisModel(modelPath, modelEnabled);
    if (errorOut) errorOut->clear();
    return true;
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
    m_connectRetryScheduled = false;
    m_connectAttempts = 0;
    ++m_lifecycleGeneration;
    m_pendingTransportMode.clear();
    m_pendingTransportEndpoint.clear();
    setLifecycleState(LifecycleState::ProcessStarting, QStringLiteral("starting core process"));

    QStringList args{QStringLiteral("--server"), m_serverName, QStringLiteral("--profile"), m_runtimeProfile.key()};
    args += extraArgs;
    m_process.setProgram(exe.absoluteFilePath());
    m_process.setArguments(args);
    m_process.setWorkingDirectory(exe.absolutePath());
    m_process.start();
    if (!m_process.waitForStarted(3000)) {
        if (errorOut) *errorOut = QStringLiteral("failed to start core process: %1").arg(m_process.errorString());
        setLifecycleState(LifecycleState::Degraded, errorOut ? *errorOut : m_process.errorString());
        return false;
    }

    setLifecycleState(LifecycleState::ProcessStarting, m_lastMessage);
    scheduleStartupTimeout();
    scheduleConnectIpc(150);
    if (errorOut) errorOut->clear();
    return true;
}

void CoreProcessClientRuntime::connectClientSignals() {
    connect(&m_client, &CoreIpcClientRuntime::connectedChanged, this, [this](bool connected) {
        if (connected) {
            m_connectRetryScheduled = false;
            m_connectAttempts = 0;
            setLifecycleState(LifecycleState::IpcConnected, QStringLiteral("core IPC connected"));
            sendPendingTransportStartIfReady();
        } else if (isActive()) {
            setLifecycleState(LifecycleState::Degraded, QStringLiteral("core IPC disconnected"));
            scheduleConnectIpc(250);
        } else {
            setLifecycleState(LifecycleState::Stopped, QStringLiteral("core IPC disconnected"));
        }
    });
    connect(&m_client, &CoreIpcClientRuntime::viewChanged, this, [this](const QJsonObject& change) {
        noteViewChanged(change);
        emit viewChanged(change);
    });
    connect(&m_client,
            &CoreIpcClientRuntime::viewSnapshotReceived,
            this,
            &CoreProcessClientRuntime::viewSnapshotReady);
    connect(&m_client, &CoreIpcClientRuntime::hostFrameWriteResult, this, [this](quint64, bool ok, const QString& summary, quint64 bytesWritten) {
        emit hostFrameWriteResult(ok, summary, bytesWritten);
    });
    connect(&m_client,
            &CoreIpcClientRuntime::captureStorageUpdate,
            this,
            [this](quint64,
                   bool ok,
                   const QString& error,
                   bool stateChanged,
                   bool active,
                   const QString& path,
                   bool progressDue,
                   quint64 bytesWritten,
                   quint64 recordCount) {
                emit captureStorageUpdate(ok, error, stateChanged, active, path, progressDue, bytesWritten, recordCount);
            });
    connect(&m_client, &CoreIpcClientRuntime::errorReceived, this, [this](quint64 requestId, const QString& error, const QString& detail) {
        const QString message = detail.isEmpty() ? error : QStringLiteral("%1: %2").arg(error, detail);
        emit ipcRequestError(requestId, error, detail);
        emit errorOccurred(QStringLiteral("core IPC error: %1").arg(message));
        if (!m_client.isConnected()) {
            setLifecycleState(LifecycleState::Degraded, QStringLiteral("core IPC error: %1").arg(message));
        }
        if (isActive() && !m_client.isConnected()) {
            scheduleConnectIpc(250);
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
            setLifecycleState(m_client.isConnected() ? LifecycleState::IpcConnected : LifecycleState::IpcConnecting,
                              QStringLiteral("core process ready"));
            scheduleConnectIpc(0);
        }
    }
}

void CoreProcessClientRuntime::readStandardError() {
    const QString message = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    if (message.isEmpty()) return;
    emit errorOccurred(QStringLiteral("core process stderr: %1").arg(message));
}

void CoreProcessClientRuntime::connectIpc() {
    m_connectRetryScheduled = false;
    if (!isActive() || m_client.isConnected() || m_serverName.isEmpty()) return;
    ++m_connectAttempts;
    setLifecycleState(LifecycleState::IpcConnecting,
                      QStringLiteral("connecting core IPC attempt %1").arg(m_connectAttempts));
    m_client.connectToServer(m_serverName);
    if (!m_client.isConnected()) scheduleConnectIpc(m_startupSeen ? 250 : 150);
}

void CoreProcessClientRuntime::scheduleConnectIpc(int delayMs) {
    if (m_connectRetryScheduled || !isActive() || m_client.isConnected() || m_serverName.isEmpty()) return;
    m_connectRetryScheduled = true;
    QTimer::singleShot(std::max(0, delayMs), this, &CoreProcessClientRuntime::connectIpc);
}

void CoreProcessClientRuntime::scheduleStartupTimeout() {
    const quint64 generation = m_lifecycleGeneration;
    QTimer::singleShot(5000, this, [this, generation]() {
        if (generation != m_lifecycleGeneration || !isActive() || m_client.isConnected()) return;
        if (m_startupSeen) ++m_ipcConnectTimeouts;
        else ++m_startupTimeouts;
        setLifecycleState(LifecycleState::Degraded,
                          m_startupSeen
                              ? QStringLiteral("core IPC connect timeout")
                              : QStringLiteral("core process startup timeout"));
        scheduleConnectIpc(250);
    });
    QTimer::singleShot(5000, this, [this, generation]() {
        if (generation != m_lifecycleGeneration || !isActive() || !m_transportOpenPending) return;
        ++m_transportOpenTimeouts;
        setLifecycleState(LifecycleState::Degraded, QStringLiteral("core transport open timeout"));
    });
}

void CoreProcessClientRuntime::setLifecycleState(LifecycleState state, const QString& message) {
    const bool lifecycleChanged = m_lifecycleState != state;
    m_lifecycleState = state;
    if (lifecycleChanged || m_lifecycleStateChangedMs == 0) {
        m_lifecycleStateChangedMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (!message.isEmpty()) {
        m_lastMessage = message;
    }
    emit stateChanged(isActive(), m_lastMessage);
}

QString CoreProcessClientRuntime::lifecycleStateText(LifecycleState state) {
    switch (state) {
    case LifecycleState::Stopped:
        return QStringLiteral("stopped");
    case LifecycleState::ProcessStarting:
        return QStringLiteral("process_starting");
    case LifecycleState::IpcConnecting:
        return QStringLiteral("ipc_connecting");
    case LifecycleState::IpcConnected:
        return QStringLiteral("ipc_connected");
    case LifecycleState::TransportOpening:
        return QStringLiteral("transport_opening");
    case LifecycleState::TransportConnected:
        return QStringLiteral("transport_connected");
    case LifecycleState::Degraded:
        return QStringLiteral("degraded");
    case LifecycleState::Stopping:
        return QStringLiteral("stopping");
    }
    return QStringLiteral("unknown");
}

bool CoreProcessClientRuntime::queuePendingTransportStart(const QString& mode, const QString& endpoint, QString* errorOut) {
    if (mode.trimmed().isEmpty() || endpoint.trimmed().isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("invalid pending core transport start");
        return false;
    }
    m_pendingTransportMode = mode;
    m_pendingTransportEndpoint = endpoint;
    scheduleConnectIpc(0);
    if (errorOut) errorOut->clear();
    return true;
}

void CoreProcessClientRuntime::sendPendingTransportStartIfReady() {
    if (!m_client.isConnected() || m_pendingTransportMode.isEmpty() || m_pendingTransportEndpoint.isEmpty()) {
        return;
    }
    m_client.startTransport(m_pendingTransportMode, m_pendingTransportEndpoint);
    m_transportOpenPending = true;
    setLifecycleState(LifecycleState::TransportOpening,
                      QStringLiteral("opening core %1 transport").arg(m_pendingTransportMode));
    scheduleStartupTimeout();
    m_pendingTransportMode.clear();
    m_pendingTransportEndpoint.clear();
}

void CoreProcessClientRuntime::noteViewChanged(const QJsonObject& change) {
    if (change.value(QStringLiteral("view_name")).toString() != QStringLiteral("transport_summary")) return;
    const QJsonObject counts = change.value(QStringLiteral("cheap_counts")).toObject();
    if (counts.value(QStringLiteral("connected")).isBool()) {
        const bool connected = counts.value(QStringLiteral("connected")).toBool();
        m_transportOpenPending = false;
        setLifecycleState(connected ? LifecycleState::TransportConnected : LifecycleState::Degraded,
                          connected ? QStringLiteral("core transport connected") : QStringLiteral("core transport disconnected"));
        return;
    }
    const QString transport = counts.value(QStringLiteral("transport")).toString();
    if (transport.startsWith(QStringLiteral("opening_"))) {
        m_transportOpenPending = true;
        setLifecycleState(LifecycleState::TransportOpening, QStringLiteral("core transport %1").arg(transport));
    } else if (transport == QStringLiteral("stopped") || transport == QStringLiteral("idle")) {
        m_transportOpenPending = false;
        setLifecycleState(m_client.isConnected() ? LifecycleState::IpcConnected : LifecycleState::IpcConnecting,
                          QStringLiteral("core transport %1").arg(transport));
    }
}

QString CoreProcessClientRuntime::makeServerName() {
    return QStringLiteral("vsm-core-ui-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
}

} // namespace CanMonitorCore
