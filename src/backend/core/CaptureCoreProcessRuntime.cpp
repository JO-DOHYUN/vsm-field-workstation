#include "core/CaptureCoreProcessRuntime.h"

#include "backend/BuildMetadata.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QMetaObject>
#include <QStringList>

namespace CanMonitorCore {

namespace {

QJsonObject buildInfoJson() {
    return QJsonObject::fromVariantMap(BuildMetadata::toVariantMap(BuildMetadata::current()));
}

CoreViewSeverity severityFromJson(const QJsonObject& object) {
    CoreViewSeverity severity = CoreViewSeverity::Ok;
    coreViewSeverityFromString(object.value(QStringLiteral("severity")).toString(), &severity);
    return severity;
}

} // namespace

CaptureCoreProcessRuntime::CaptureCoreProcessRuntime(QObject* parent)
    : QObject(parent)
    , m_ipc(&m_viewStore, this) {
    connect(&m_ipc,
            &CoreIpcServerRuntime::hostFrameRequested,
            this,
            &CaptureCoreProcessRuntime::handleHostFrameRequested,
            Qt::QueuedConnection);
    connect(&m_ipc,
            &CoreIpcServerRuntime::captureStartRequested,
            this,
            &CaptureCoreProcessRuntime::handleCaptureStartRequested,
            Qt::QueuedConnection);
    connect(&m_ipc,
            &CoreIpcServerRuntime::captureStopRequested,
            this,
            &CaptureCoreProcessRuntime::handleCaptureStopRequested,
            Qt::QueuedConnection);
}

CaptureCoreProcessRuntime::~CaptureCoreProcessRuntime() {
    stopTransport();
    stopIpc();
}

bool CaptureCoreProcessRuntime::startIpc(const QString& serverName, QString* errorOut) {
    seedInitialViews();
    if (!m_ipc.listen(serverName, errorOut)) {
        return false;
    }
    emit ipcReady(serverName);
    return true;
}

void CaptureCoreProcessRuntime::stopIpc() {
    m_ipc.close();
}

bool CaptureCoreProcessRuntime::isIpcListening() const {
    return m_ipc.isListening();
}

QString CaptureCoreProcessRuntime::serverName() const {
    return m_ipc.serverName();
}

void CaptureCoreProcessRuntime::startSerial(const QString& portName) {
    ensureTransportRuntime();
    updateTransportSummary(QJsonObject{{QStringLiteral("transport"), QStringLiteral("opening_serial")},
                                       {QStringLiteral("endpoint"), portName},
                                       {QStringLiteral("serial_owner"), QStringLiteral("core")}},
                           CoreViewSeverity::Warn,
                           QJsonObject{{QStringLiteral("transport"), QStringLiteral("opening_serial")}});
    QMetaObject::invokeMethod(m_drainRuntime,
                              "startSerial",
                              Qt::QueuedConnection,
                              Q_ARG(QString, portName));
}

void CaptureCoreProcessRuntime::startGatewayTcp(const QString& endpoint) {
    ensureTransportRuntime();
    updateTransportSummary(QJsonObject{{QStringLiteral("transport"), QStringLiteral("opening_gateway_tcp")},
                                       {QStringLiteral("endpoint"), endpoint},
                                       {QStringLiteral("serial_owner"), QStringLiteral("core")}},
                           CoreViewSeverity::Warn,
                           QJsonObject{{QStringLiteral("transport"), QStringLiteral("opening_gateway_tcp")}});
    QMetaObject::invokeMethod(m_drainRuntime,
                              "startGatewayTcp",
                              Qt::QueuedConnection,
                              Q_ARG(QString, endpoint));
}

void CaptureCoreProcessRuntime::stopTransport() {
    if (!m_transportRuntimeStarted) return;
    teardownTransportRuntime();
    m_transportConnected = false;
    m_transportMessage = QStringLiteral("stopped");
    updateTransportSummary(QJsonObject{{QStringLiteral("transport"), QStringLiteral("stopped")},
                                       {QStringLiteral("serial_owner"), QStringLiteral("core")},
                                       {QStringLiteral("capture_active"), false}},
                           CoreViewSeverity::Ok,
                           QJsonObject{{QStringLiteral("transport"), QStringLiteral("stopped")}});
}

QJsonObject CaptureCoreProcessRuntime::statusJson() const {
    return QJsonObject{{QStringLiteral("ipc_listening"), m_ipc.isListening()},
                       {QStringLiteral("server_name"), m_ipc.serverName()},
                       {QStringLiteral("transport_started"), m_transportRuntimeStarted},
                       {QStringLiteral("transport_connected"), m_transportConnected},
                       {QStringLiteral("transport_message"), m_transportMessage}};
}

void CaptureCoreProcessRuntime::ensureTransportRuntime() {
    if (m_transportRuntimeStarted) return;

    m_drainQueue = QSharedPointer<CanMonitorTransport::DrainByteQueue>::create();
    m_captureQueue = QSharedPointer<CanMonitorTransport::TypedRecordHandoffQueue>::create();

    auto* drain = new CanMonitorTransport::SerialDrainRuntime(m_drainQueue);
    auto* pipeline = new CanMonitorTransport::TypedEvidencePipelineWorkerRuntime(m_drainQueue, m_captureQueue);
    m_drainRuntime = drain;
    m_pipelineRuntime = pipeline;

    drain->moveToThread(&m_drainThread);
    pipeline->moveToThread(&m_pipelineThread);

    connect(&m_drainThread, &QThread::finished, drain, &QObject::deleteLater);
    connect(&m_pipelineThread, &QThread::finished, pipeline, &QObject::deleteLater);
    connect(drain,
            &CanMonitorTransport::SerialDrainRuntime::bytesAvailable,
            pipeline,
            [pipeline]() { pipeline->schedulePump(-1); },
            Qt::QueuedConnection);
    connect(pipeline,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::pumpCycleFinished,
            drain,
            &CanMonitorTransport::SerialDrainRuntime::acknowledgeBytesAvailable,
            Qt::QueuedConnection);
    connect(drain,
            &CanMonitorTransport::SerialDrainRuntime::stateChanged,
            this,
            [this](bool connected, const QString& message) {
                m_transportConnected = connected;
                m_transportMessage = message;
                updateTransportSummary(QJsonObject{{QStringLiteral("transport"), connected ? QStringLiteral("connected") : QStringLiteral("disconnected")},
                                                   {QStringLiteral("message"), message},
                                                   {QStringLiteral("serial_owner"), QStringLiteral("core")},
                                                   {QStringLiteral("capture_active"), false}},
                                       connected ? CoreViewSeverity::Ok : CoreViewSeverity::Warn,
                                       QJsonObject{{QStringLiteral("connected"), connected}});
                emit transportStateChanged(connected, message);
            },
            Qt::QueuedConnection);
    connect(drain, &CanMonitorTransport::SerialDrainRuntime::errorOccurred, this, [this](const QString& message) {
        emit errorOccurred(message);
        updateTransportSummary(QJsonObject{{QStringLiteral("transport"), QStringLiteral("error")},
                                           {QStringLiteral("message"), message},
                                           {QStringLiteral("serial_owner"), QStringLiteral("core")}},
                               CoreViewSeverity::Error,
                               QJsonObject{{QStringLiteral("error"), true}});
    });
    connect(drain,
            &CanMonitorTransport::SerialDrainRuntime::drainEventTraceChanged,
            this,
            [this](const QJsonObject& trace) {
                updateTransportSummary(QJsonObject{{QStringLiteral("transport"), m_transportConnected ? QStringLiteral("connected") : QStringLiteral("idle")},
                                                   {QStringLiteral("message"), m_transportMessage},
                                                   {QStringLiteral("serial_owner"), QStringLiteral("core")},
                                                   {QStringLiteral("drain_event_trace"), trace}},
                                       m_transportConnected ? CoreViewSeverity::Ok : CoreViewSeverity::Warn,
                                       QJsonObject{{QStringLiteral("raw_queue_used_bytes"), trace.value(QStringLiteral("drain_queue_used_bytes")).toVariant().toString()},
                                                   {QStringLiteral("raw_queue_overrun_bytes"), trace.value(QStringLiteral("drain_queue_overrun_bytes")).toVariant().toString()}});
            },
            Qt::QueuedConnection);
    connect(drain,
            &CanMonitorTransport::SerialDrainRuntime::hostFrameWriteResult,
            this,
            &CaptureCoreProcessRuntime::publishHostFrameWriteResult,
            Qt::QueuedConnection);
    connect(pipeline,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::coreViewChanged,
            this,
            &CaptureCoreProcessRuntime::requestPipelineViewMirror,
            Qt::QueuedConnection);
    connect(pipeline,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::captureQueueReady,
            this,
            [this]() {
                if (!m_captureWriterRuntime) return;
                QMetaObject::invokeMethod(m_captureWriterRuntime,
                                          &CanMonitorTransport::TypedCaptureWriterWorkerRuntime::drainQueuedRecords,
                                          Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    connect(pipeline,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::captureHandoffOverrun,
            this,
            [this](quint64 records, quint64 bytes, const QString& reason) {
                if (!m_captureWriterRuntime) {
                    emit errorOccurred(reason);
                    return;
                }
                QMetaObject::invokeMethod(m_captureWriterRuntime,
                                          [worker = QPointer<CanMonitorTransport::TypedCaptureWriterWorkerRuntime>(m_captureWriterRuntime),
                                           records,
                                           bytes,
                                           reason]() {
                                              if (worker) worker->noteOverrun(records, bytes, reason);
                                          },
                                          Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    connect(pipeline,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::coreViewSnapshotReady,
            this,
            &CaptureCoreProcessRuntime::applyPipelineSnapshot,
            Qt::QueuedConnection);
    connect(pipeline,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::errorsOccurred,
            this,
            [this](const QStringList& errors) {
                if (errors.isEmpty()) return;
                emit errorOccurred(errors.join(QStringLiteral("; ")));
            },
            Qt::QueuedConnection);

    m_drainThread.setObjectName(QStringLiteral("vsm-core-drain"));
    m_pipelineThread.setObjectName(QStringLiteral("vsm-core-pipeline"));
    m_drainThread.start(QThread::TimeCriticalPriority);
    m_pipelineThread.start(QThread::HighPriority);
    m_transportRuntimeStarted = true;
    updateCoreHealth(QStringLiteral("transport_runtime_started"));
}

void CaptureCoreProcessRuntime::teardownTransportRuntime() {
    shutdownCaptureWriterRuntime();
    if (m_drainRuntime) {
        QMetaObject::invokeMethod(m_drainRuntime,
                                  &CanMonitorTransport::SerialDrainRuntime::stop,
                                  Qt::BlockingQueuedConnection);
    }
    m_drainThread.quit();
    m_pipelineThread.quit();
    m_drainThread.wait(3000);
    m_pipelineThread.wait(3000);
    m_drainRuntime = nullptr;
    m_pipelineRuntime = nullptr;
    m_drainQueue.clear();
    m_captureQueue.clear();
    m_pendingMirrorRequests.clear();
    m_pendingHostFrameRequests.clear();
    m_transportRuntimeStarted = false;
}

void CaptureCoreProcessRuntime::ensureCaptureWriterRuntime() {
    if (m_captureWriterRuntime) return;
    if (!m_captureQueue) {
        m_captureQueue = QSharedPointer<CanMonitorTransport::TypedRecordHandoffQueue>::create();
    }
    auto* writer = new CanMonitorTransport::TypedCaptureWriterWorkerRuntime();
    writer->setRecordQueue(m_captureQueue);
    m_captureWriterRuntime = writer;
    writer->moveToThread(&m_captureWriterThread);
    connect(&m_captureWriterThread, &QThread::finished, writer, &QObject::deleteLater);
    connect(writer,
            &CanMonitorTransport::TypedCaptureWriterWorkerRuntime::storageUpdate,
            this,
            [this](bool ok,
                   const QString& error,
                   bool stateChanged,
                   bool active,
                   const QString& path,
                   bool progressDue,
                   quint64 bytesWritten,
                   quint64 recordCount) {
                CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate update;
                update.ok = ok;
                update.error = error;
                update.stateChanged = stateChanged;
                update.active = active;
                update.path = path;
                update.progressDue = progressDue;
                update.bytesWritten = bytesWritten;
                update.recordCount = recordCount;
                CanMonitorTransport::TypedCaptureWriterRuntime::Status status;
                status.active = active;
                updateCaptureProgressView(status, &update);
            },
            Qt::QueuedConnection);
    connect(writer,
            &CanMonitorTransport::TypedCaptureWriterWorkerRuntime::statusChanged,
            this,
            [this](bool active,
                   bool captureInvalid,
                   quint64 queuedRecords,
                   quint64 queuedBytes,
                   quint64 maxQueuedBytes,
                   quint64 overrunRecords,
                   quint64 overrunBytes,
                   quint64 writeMaxMs) {
                CanMonitorTransport::TypedCaptureWriterRuntime::Status status;
                status.active = active;
                status.captureInvalid = captureInvalid;
                status.queuedRecords = queuedRecords;
                status.queuedBytes = queuedBytes;
                status.maxQueuedBytes = maxQueuedBytes;
                status.overrunRecords = overrunRecords;
                status.overrunBytes = overrunBytes;
                status.writeMaxMs = writeMaxMs;
                updateCaptureProgressView(status);
            },
            Qt::QueuedConnection);
    m_captureWriterThread.setObjectName(QStringLiteral("vsm-core-capture-writer"));
    m_captureWriterThread.start(QThread::HighPriority);
}

void CaptureCoreProcessRuntime::shutdownCaptureWriterRuntime() {
    if (m_captureWriterRuntime) {
        setPipelineCaptureEnabled(false, Qt::BlockingQueuedConnection);
        drainCaptureQueueSync();
        QMetaObject::invokeMethod(m_captureWriterRuntime,
                                  &CanMonitorTransport::TypedCaptureWriterWorkerRuntime::resetQueue,
                                  Qt::BlockingQueuedConnection);
    }
    if (m_captureWriterThread.isRunning()) {
        m_captureWriterThread.quit();
        m_captureWriterThread.wait(3000);
    }
    m_captureWriterRuntime = nullptr;
    if (m_captureQueue) m_captureQueue->clear();
}

void CaptureCoreProcessRuntime::seedInitialViews() {
    m_viewStore.clear();
    updateCoreHealth(QStringLiteral("ready"));
    updateTransportSummary(QJsonObject{{QStringLiteral("transport"), QStringLiteral("idle")},
                                       {QStringLiteral("serial_owner"), QStringLiteral("core")},
                                       {QStringLiteral("capture_active"), false}},
                           CoreViewSeverity::Ok,
                           QJsonObject{{QStringLiteral("transport"), QStringLiteral("idle")}});
}

void CaptureCoreProcessRuntime::updateCoreHealth(const QString& state, CoreViewSeverity severity) {
    publishChange(m_viewStore.updateView(CoreViewName::CoreHealth,
                                         QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-capture-core")},
                                                     {QStringLiteral("state"), state},
                                                     {QStringLiteral("build"), buildInfoJson()},
                                                     {QStringLiteral("pid"), QString::number(QCoreApplication::applicationPid())}},
                                         severity,
                                         QJsonObject{{QStringLiteral("state"), state}}));
}

void CaptureCoreProcessRuntime::updateTransportSummary(const QJsonObject& payload,
                                                       CoreViewSeverity severity,
                                                       const QJsonObject& cheapCounts) {
    publishChange(m_viewStore.updateView(CoreViewName::TransportSummary, payload, severity, cheapCounts));
}

void CaptureCoreProcessRuntime::publishChange(const ViewChanged& change) {
    if (m_ipc.isListening()) {
        m_ipc.publishViewChanged(change);
    }
}

void CaptureCoreProcessRuntime::requestPipelineViewMirror(const QJsonObject& change) {
    if (!m_pipelineRuntime) return;

    CoreViewName viewName = CoreViewName::CoreHealth;
    if (!coreViewNameFromString(change.value(QStringLiteral("view_name")).toString(), &viewName)) return;

    const quint64 requestId = m_nextMirrorRequestId++;
    m_pendingMirrorRequests.insert(requestId, viewName);
    QMetaObject::invokeMethod(m_pipelineRuntime,
                              "queryCoreView",
                              Qt::QueuedConnection,
                              Q_ARG(QString, coreViewNameToString(viewName)),
                              Q_ARG(quint64, 0),
                              Q_ARG(int, 256),
                              Q_ARG(quint64, requestId));
}

void CaptureCoreProcessRuntime::applyPipelineSnapshot(quint64 requestId,
                                                      bool changed,
                                                      const QJsonObject& snapshot,
                                                      const QJsonObject& change) {
    const auto it = m_pendingMirrorRequests.find(requestId);
    if (it == m_pendingMirrorRequests.end()) return;
    const CoreViewName viewName = it.value();
    m_pendingMirrorRequests.erase(it);
    if (!changed) return;

    const QJsonObject payload = snapshot.value(QStringLiteral("payload")).toObject();
    const QJsonObject cheapCounts = change.value(QStringLiteral("cheap_counts")).toObject();
    publishChange(m_viewStore.updateView(viewName,
                                         payload,
                                         severityFromJson(snapshot),
                                         cheapCounts));
}

void CaptureCoreProcessRuntime::handleHostFrameRequested(quint64 requestId, const QByteArray& frame, const QString& summary) {
    if (frame.isEmpty()) {
        m_ipc.publishHostFrameWriteResult(requestId, false, summary, 0);
        return;
    }
    if (!m_drainRuntime || !m_transportRuntimeStarted || !m_transportConnected) {
        m_ipc.publishHostFrameWriteResult(requestId, false, QStringLiteral("%1 | core transport not connected").arg(summary), 0);
        return;
    }
    m_pendingHostFrameRequests.enqueue(requestId);
    QMetaObject::invokeMethod(m_drainRuntime,
                              "sendHostFrame",
                              Qt::QueuedConnection,
                              Q_ARG(QByteArray, frame),
                              Q_ARG(QString, summary));
}

void CaptureCoreProcessRuntime::publishHostFrameWriteResult(bool ok, const QString& summary, quint64 bytesWritten) {
    const quint64 requestId = m_pendingHostFrameRequests.isEmpty() ? 0 : m_pendingHostFrameRequests.dequeue();
    m_ipc.publishHostFrameWriteResult(requestId, ok, summary, bytesWritten);
}

void CaptureCoreProcessRuntime::handleCaptureStartRequested(quint64 requestId,
                                                            const QString& sessionDir,
                                                            const QJsonObject& metadata) {
    ensureTransportRuntime();
    ensureCaptureWriterRuntime();
    if (m_captureQueue) m_captureQueue->clear();

    CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate update;
    QMetaObject::invokeMethod(m_captureWriterRuntime,
                              [worker = QPointer<CanMonitorTransport::TypedCaptureWriterWorkerRuntime>(m_captureWriterRuntime),
                               sessionDir,
                               metadata,
                               &update]() {
                                  if (worker) update = worker->startStorageSync(sessionDir, metadata);
                              },
                              Qt::BlockingQueuedConnection);
    if (update.ok) {
        setPipelineCaptureEnabled(true, Qt::BlockingQueuedConnection);
    }
    publishCaptureStorageUpdate(requestId, update);
    CanMonitorTransport::TypedCaptureWriterRuntime::Status status;
    status.active = update.active;
    updateCaptureProgressView(status, &update);
}

void CaptureCoreProcessRuntime::handleCaptureStopRequested(quint64 requestId,
                                                           const QString& inactivePath,
                                                           const QJsonObject& diagnostics) {
    if (!m_captureWriterRuntime) {
        CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate update;
        update.ok = false;
        update.error = QStringLiteral("core capture writer is not running");
        update.stateChanged = true;
        update.active = false;
        update.path = inactivePath;
        publishCaptureStorageUpdate(requestId, update);
        return;
    }

    setPipelineCaptureEnabled(false, Qt::BlockingQueuedConnection);
    drainCaptureQueueSync();

    CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate update;
    QMetaObject::invokeMethod(m_captureWriterRuntime,
                              [worker = QPointer<CanMonitorTransport::TypedCaptureWriterWorkerRuntime>(m_captureWriterRuntime),
                               inactivePath,
                               diagnostics,
                               &update]() {
                                  if (worker) update = worker->stopStorageSync(inactivePath, diagnostics);
                              },
                              Qt::BlockingQueuedConnection);
    publishCaptureStorageUpdate(requestId, update);
    CanMonitorTransport::TypedCaptureWriterRuntime::Status status;
    status.active = update.active;
    updateCaptureProgressView(status, &update);
}

void CaptureCoreProcessRuntime::setPipelineCaptureEnabled(bool enabled, Qt::ConnectionType connectionType) {
    if (!m_pipelineRuntime) return;
    QMetaObject::invokeMethod(m_pipelineRuntime,
                              [worker = QPointer<CanMonitorTransport::TypedEvidencePipelineWorkerRuntime>(m_pipelineRuntime), enabled]() {
                                  if (worker) worker->setCaptureEnabled(enabled);
                              },
                              connectionType);
}

void CaptureCoreProcessRuntime::drainCaptureQueueSync() {
    if (!m_captureWriterRuntime) return;
    while (m_captureQueue && m_captureQueue->hasQueuedRecords()) {
        QMetaObject::invokeMethod(m_captureWriterRuntime,
                                  &CanMonitorTransport::TypedCaptureWriterWorkerRuntime::drainQueuedRecords,
                                  Qt::BlockingQueuedConnection);
    }
}

void CaptureCoreProcessRuntime::publishCaptureStorageUpdate(
    quint64 requestId,
    const CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate& update) {
    m_ipc.publishCaptureStorageUpdate(requestId,
                                      update.ok,
                                      update.error,
                                      update.stateChanged,
                                      update.active,
                                      update.path,
                                      update.progressDue,
                                      update.bytesWritten,
                                      update.recordCount);
}

void CaptureCoreProcessRuntime::updateCaptureProgressView(
    const CanMonitorTransport::TypedCaptureWriterRuntime::Status& status,
    const CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate* update) {
    QJsonObject payload;
    payload.insert(QStringLiteral("capture_active"), status.active);
    payload.insert(QStringLiteral("capture_invalid"), status.captureInvalid);
    payload.insert(QStringLiteral("queued_records"), QString::number(status.queuedRecords));
    payload.insert(QStringLiteral("queued_bytes"), QString::number(status.queuedBytes));
    payload.insert(QStringLiteral("max_queued_bytes"), QString::number(status.maxQueuedBytes));
    payload.insert(QStringLiteral("overrun_records"), QString::number(status.overrunRecords));
    payload.insert(QStringLiteral("overrun_bytes"), QString::number(status.overrunBytes));
    payload.insert(QStringLiteral("write_max_ms"), QString::number(status.writeMaxMs));
    if (update) {
        payload.insert(QStringLiteral("ok"), update->ok);
        payload.insert(QStringLiteral("state_changed"), update->stateChanged);
        payload.insert(QStringLiteral("path"), update->path);
        payload.insert(QStringLiteral("bytes_written"), QString::number(update->bytesWritten));
        payload.insert(QStringLiteral("record_count"), QString::number(update->recordCount));
        if (!update->error.isEmpty()) payload.insert(QStringLiteral("error"), update->error);
    }

    QJsonObject counts;
    counts.insert(QStringLiteral("queued_bytes"), QString::number(status.queuedBytes));
    counts.insert(QStringLiteral("overrun_bytes"), QString::number(status.overrunBytes));
    counts.insert(QStringLiteral("record_count"), update ? QString::number(update->recordCount) : QStringLiteral("0"));

    publishChange(m_viewStore.updateView(CanMonitorCore::CoreViewName::CaptureProgress,
                                         payload,
                                         status.captureInvalid || (update && !update->ok)
                                             ? CanMonitorCore::CoreViewSeverity::Error
                                             : CanMonitorCore::CoreViewSeverity::Ok,
                                         counts));
}

} // namespace CanMonitorCore
