#include "core/CaptureCoreProcessRuntime.h"

#include "backend/BuildMetadata.h"
#include "backend/ModelPack.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QStringList>
#include <QTimer>

#include <algorithm>

namespace CanMonitorCore {

namespace {

QJsonObject buildInfoJson() {
    return QJsonObject::fromVariantMap(BuildMetadata::toVariantMap(BuildMetadata::current()));
}

constexpr int kCoreRawLedgerPendingFrameCap = 4096;
constexpr int kCoreRawLedgerTailViewCap = 1024;

CoreViewSeverity maxSeverity(CoreViewSeverity lhs, CoreViewSeverity rhs) {
    return static_cast<int>(lhs) >= static_cast<int>(rhs) ? lhs : rhs;
}

CoreViewSeverity severityFromJson(const QJsonObject& object) {
    CoreViewSeverity severity = CoreViewSeverity::Ok;
    coreViewSeverityFromString(object.value(QStringLiteral("severity")).toString(), &severity);
    return severity;
}

QString frameDataHex(const FrameRecord& frame) {
    QByteArray bytes;
    bytes.reserve(8);
    for (quint8 byte : frame.data) {
        bytes.append(char(byte));
    }
    return QString::fromLatin1(bytes.toHex());
}

QJsonObject frameToViewRow(const FrameRecord& frame, quint64 ledgerSeq) {
    QJsonObject row;
    row.insert(QStringLiteral("ledger_seq"), QString::number(ledgerSeq));
    row.insert(QStringLiteral("mono_us"), QString::number(frame.tExtUs));
    row.insert(QStringLiteral("bus"), int(frame.bus));
    row.insert(QStringLiteral("can_id"), int(frame.canId));
    row.insert(QStringLiteral("ext"), frame.ext);
    row.insert(QStringLiteral("rtr"), frame.rtr);
    row.insert(QStringLiteral("dlc"), int(frame.dlc));
    row.insert(QStringLiteral("data_hex"), frameDataHex(frame));
    row.insert(QStringLiteral("seq"), int(frame.seq));
    row.insert(QStringLiteral("typed_seq_lsb"), int(frame.seq));
    row.insert(QStringLiteral("record_type"), QStringLiteral("CAN_RX_SEGMENT"));
    row.insert(QStringLiteral("evidence_kind"), QStringLiteral("decoded_can_tail_row"));
    if (frame.hasCaptureSeq) {
        row.insert(QStringLiteral("capture_seq"), QString::number(frame.captureSeq));
    }
    return row;
}

void addDecodedTailSemantics(QJsonObject& payload) {
    payload.insert(QStringLiteral("source"), QStringLiteral("decoded_can_tail_view"));
    payload.insert(QStringLiteral("evidence_kind"), QStringLiteral("decoded_can_tail"));
    payload.insert(QStringLiteral("truth_source"), QStringLiteral("capture.stream/index"));
    payload.insert(QStringLiteral("truth_semantics"), QStringLiteral("derived_display_tail_not_raw_evidence"));
    payload.insert(QStringLiteral("drop_semantics"), QStringLiteral("display_tail_drop_not_capture_truth_loss"));
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
            &CoreIpcServerRuntime::controlCycleRequested,
            this,
            &CaptureCoreProcessRuntime::handleControlCycleRequested,
            Qt::QueuedConnection);
    connect(&m_ipc,
            &CoreIpcServerRuntime::transportStartRequested,
            this,
            [this](quint64, const QString& mode, const QString& endpoint) {
                if (mode == QStringLiteral("gateway_tcp")) {
                    startGatewayTcp(endpoint);
                } else {
                    startSerial(endpoint);
                }
            },
            Qt::QueuedConnection);
    connect(&m_ipc,
            &CoreIpcServerRuntime::transportStopRequested,
            this,
            [this](quint64) { stopTransport(); },
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
    connect(&m_ipc,
            &CoreIpcServerRuntime::analysisModelRequested,
            this,
            &CaptureCoreProcessRuntime::handleAnalysisModelRequested,
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
    m_transportConnected = false;
    m_transportMessage = QStringLiteral("opening serial: %1").arg(portName);
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
    m_transportConnected = false;
    m_transportMessage = QStringLiteral("opening gateway TCP: %1").arg(endpoint);
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
    stopControlCycle();
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
    QJsonObject out{{QStringLiteral("ipc_listening"), m_ipc.isListening()},
                    {QStringLiteral("server_name"), m_ipc.serverName()},
                    {QStringLiteral("transport_started"), m_transportRuntimeStarted},
                    {QStringLiteral("transport_connected"), m_transportConnected},
                    {QStringLiteral("transport_message"), m_transportMessage}};
    out.insert(QStringLiteral("ipc"), m_ipc.statusJson());
    return out;
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
                const quint64 rawOverrunBytes = trace.value(QStringLiteral("drain_queue_overrun_bytes")).toVariant().toULongLong();
                if (rawOverrunBytes > m_lastRawIngressOverrunBytes) {
                    const quint64 deltaBytes = rawOverrunBytes - m_lastRawIngressOverrunBytes;
                    m_lastRawIngressOverrunBytes = rawOverrunBytes;
                    latchCaptureInvalid(QStringLiteral("Host drain raw ingress overrun: %1 bytes total")
                                            .arg(rawOverrunBytes),
                                        rawOverrunBytes);
                    publishFatalDiagnostic(QStringLiteral("host_drain_overrun"),
                                           QStringLiteral("Host drain raw ingress overrun"),
                                           QJsonObject{{QStringLiteral("raw_ingress_overrun_bytes"), QString::number(rawOverrunBytes)},
                                                       {QStringLiteral("delta_bytes"), QString::number(deltaBytes)}});
                    if (m_captureWriterRuntime) {
                        QMetaObject::invokeMethod(m_captureWriterRuntime,
                                                  [worker = QPointer<CanMonitorTransport::TypedCaptureWriterWorkerRuntime>(m_captureWriterRuntime),
                                                   deltaBytes,
                                                   rawOverrunBytes]() {
                                                      if (worker) {
                                                          worker->noteOverrun(0,
                                                                              deltaBytes,
                                                                              QStringLiteral("Host drain raw ingress overrun: %1 bytes total")
                                                                                  .arg(rawOverrunBytes));
                                                      }
                                                  },
                                                  Qt::QueuedConnection);
                    }
                }
                updateTransportSummary(QJsonObject{{QStringLiteral("transport"), m_transportConnected ? QStringLiteral("connected") : QStringLiteral("idle")},
                                                   {QStringLiteral("message"), m_transportMessage},
                                                   {QStringLiteral("serial_owner"), QStringLiteral("core")},
                                                   {QStringLiteral("drain_event_trace"), trace},
                                                   {QStringLiteral("host_drain_overrun"), rawOverrunBytes > 0},
                                                   {QStringLiteral("raw_ingress_invalid"), rawOverrunBytes > 0},
                                                   {QStringLiteral("raw_ingress_overrun_bytes"), QString::number(rawOverrunBytes)}},
                                       rawOverrunBytes > 0
                                           ? CoreViewSeverity::Fatal
                                           : (m_transportConnected ? CoreViewSeverity::Ok : CoreViewSeverity::Warn),
                                       QJsonObject{{QStringLiteral("raw_queue_used_bytes"), trace.value(QStringLiteral("drain_queue_used_bytes")).toVariant().toString()},
                                                   {QStringLiteral("raw_queue_overrun_bytes"), QString::number(rawOverrunBytes)}});
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
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::rawLedgerFramesReady,
            this,
            static_cast<void (CaptureCoreProcessRuntime::*)(const CanMonitorTransport::RawLedgerFrameBatch&)>(
                &CaptureCoreProcessRuntime::queueRawLedgerFrames),
            Qt::QueuedConnection);
    connect(pipeline,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::analysisFramesReady,
            this,
            &CaptureCoreProcessRuntime::queueAnalysisFrames,
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
    ensureRawLedgerRuntime();
    ensureAnalysisRuntime();
    m_drainThread.start(QThread::TimeCriticalPriority);
    m_pipelineThread.start(QThread::HighPriority);
    QMetaObject::invokeMethod(pipeline,
                              "setSecondaryFanoutEnabled",
                              Qt::QueuedConnection,
                              Q_ARG(bool, true));
    m_transportRuntimeStarted = true;
    updateCoreHealth(QStringLiteral("transport_runtime_started"));
}

void CaptureCoreProcessRuntime::teardownTransportRuntime() {
    shutdownCaptureWriterRuntime();
    shutdownRawLedgerRuntime();
    shutdownAnalysisRuntime();
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
    stopControlCycle();
    m_transportRuntimeStarted = false;
}

void CaptureCoreProcessRuntime::timerEvent(QTimerEvent* event) {
    if (event->timerId() == m_controlCycleTimerId) {
        beginControlCycle();
        return;
    }
    if (event->timerId() == m_controlCycleGapTimerId) {
        if (m_controlCycleGapTimerId != 0) {
            killTimer(m_controlCycleGapTimerId);
            m_controlCycleGapTimerId = 0;
        }
        continueControlCycleBurst();
        return;
    }
    QObject::timerEvent(event);
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

void CaptureCoreProcessRuntime::ensureRawLedgerRuntime() {
    if (m_rawLedgerRuntime) return;

    auto* writer = new CanMonitorTransport::RawLedgerWriterRuntime();
    m_rawLedgerRuntime = writer;
    writer->moveToThread(&m_rawLedgerThread);
    connect(&m_rawLedgerThread, &QThread::finished, writer, &QObject::deleteLater);
    connect(writer,
            &CanMonitorTransport::RawLedgerWriterRuntime::resetCompleted,
            this,
            [this](bool ok, const QString& path, const QString& error) {
                m_rawLedgerTailRows = QJsonArray{};
                m_rawLedgerDroppedDisplayRows = 0;
                m_rawLedgerDroppedHandoffFrames = 0;
                m_rawLedgerLastTotalRows = 0;
                m_rawLedgerLastSegmentBytes = 0;
                QJsonObject payload{{QStringLiteral("ok"), ok},
                                    {QStringLiteral("path"), path},
                                    {QStringLiteral("total_rows"), QStringLiteral("0")},
                                    {QStringLiteral("segment_bytes"), QStringLiteral("0")},
                                    {QStringLiteral("frames"), QJsonArray{}},
                                    {QStringLiteral("item_count"), 0},
                                    {QStringLiteral("item_cap"), kCoreRawLedgerTailViewCap}};
                addDecodedTailSemantics(payload);
                if (!error.isEmpty()) payload.insert(QStringLiteral("error"), error);
                QJsonObject counts{{QStringLiteral("total_rows"), QStringLiteral("0")},
                                   {QStringLiteral("segment_bytes"), QStringLiteral("0")},
                                   {QStringLiteral("dropped_display_count"), QStringLiteral("0")}};
                publishChange(m_viewStore.updateView(CoreViewName::RawLedgerTail,
                                                     payload,
                                                     ok ? CoreViewSeverity::Ok : CoreViewSeverity::Error,
                                                     counts));
            },
            Qt::QueuedConnection);
    connect(writer,
            &CanMonitorTransport::RawLedgerWriterRuntime::batchCommitted,
            this,
            &CaptureCoreProcessRuntime::updateRawLedgerTailView,
            Qt::QueuedConnection);
    connect(writer,
            &CanMonitorTransport::RawLedgerWriterRuntime::statusChanged,
            this,
            &CaptureCoreProcessRuntime::updateRawLedgerStatusView,
            Qt::QueuedConnection);
    connect(writer,
            &CanMonitorTransport::RawLedgerWriterRuntime::batchFinished,
            this,
            [this]() {
                m_rawLedgerDispatchInFlight = false;
                flushRawLedgerFrames(false);
            },
            Qt::QueuedConnection);

    m_rawLedgerThread.setObjectName(QStringLiteral("vsm-core-raw-ledger"));
    m_rawLedgerThread.start(QThread::HighPriority);
    QMetaObject::invokeMethod(writer,
                              "reset",
                              Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("core")));
}

void CaptureCoreProcessRuntime::shutdownRawLedgerRuntime() {
    flushRawLedgerFrames(true);
    if (m_rawLedgerThread.isRunning()) {
        m_rawLedgerThread.quit();
        m_rawLedgerThread.wait(3000);
    }
    m_rawLedgerRuntime = nullptr;
    m_pendingRawLedgerFrames.clear();
    m_rawLedgerDispatchInFlight = false;
}

void CaptureCoreProcessRuntime::ensureAnalysisRuntime() {
    if (m_analysisRuntime) return;

    auto* worker = new CanMonitorAnalysis::AnalysisWorkerRuntime();
    m_analysisRuntime = worker;
    worker->moveToThread(&m_analysisThread);
    connect(&m_analysisThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker,
            &CanMonitorAnalysis::AnalysisWorkerRuntime::snapshotReady,
            this,
            &CaptureCoreProcessRuntime::publishAnalysisSnapshot,
            Qt::QueuedConnection);
    connect(worker,
            &CanMonitorAnalysis::AnalysisWorkerRuntime::statusChanged,
            this,
            &CaptureCoreProcessRuntime::updateAnalysisStatus,
            Qt::QueuedConnection);
    connect(worker,
            &CanMonitorAnalysis::AnalysisWorkerRuntime::errorOccurred,
            this,
            [this](const QString& message) {
                emit errorOccurred(message);
            },
            Qt::QueuedConnection);
    m_analysisThread.setObjectName(QStringLiteral("vsm-core-analysis"));
    m_analysisThread.start(QThread::HighPriority);
    QMetaObject::invokeMethod(worker, &CanMonitorAnalysis::AnalysisWorkerRuntime::reset, Qt::QueuedConnection);
}

void CaptureCoreProcessRuntime::shutdownAnalysisRuntime() {
    if (m_analysisRuntime) {
        QMetaObject::invokeMethod(m_analysisRuntime,
                                  &CanMonitorAnalysis::AnalysisWorkerRuntime::reset,
                                  Qt::BlockingQueuedConnection);
    }
    if (m_analysisThread.isRunning()) {
        m_analysisThread.quit();
        m_analysisThread.wait(3000);
    }
    m_analysisRuntime = nullptr;
}

void CaptureCoreProcessRuntime::queueAnalysisFrames(const CanMonitorTransport::AnalysisFrameBatch& batch) {
    const FrameRecordList frames = batch.frames;
    ensureAnalysisRuntime();
    if (batch.overrunFrames > 0) {
        QMetaObject::invokeMethod(m_analysisRuntime,
                                  [worker = QPointer<CanMonitorAnalysis::AnalysisWorkerRuntime>(m_analysisRuntime),
                                   overrunFrames = batch.overrunFrames]() {
                                      if (worker) {
                                          worker->noteTruthLoss(overrunFrames,
                                                                QStringLiteral("Analysis handoff overrun before core queue: %1 CAN_RX frames")
                                                                    .arg(overrunFrames));
                                      }
                                  },
                                  Qt::QueuedConnection);
    }
    if (frames.isEmpty()) return;
    QMetaObject::invokeMethod(m_analysisRuntime,
                              [worker = QPointer<CanMonitorAnalysis::AnalysisWorkerRuntime>(m_analysisRuntime),
                               frames]() mutable {
                                  if (worker) worker->enqueueFrames(std::move(frames));
                              },
                              Qt::QueuedConnection);
}

void CaptureCoreProcessRuntime::publishAnalysisSnapshot(const QString& source,
                                                        const QString& level,
                                                        const QString& summary,
                                                        const QVariantList& diagnostics,
                                                        const QVariantList& timingRows,
                                                        const QVariantList& valueRows,
                                                        const QVariantList& alarmRows) {
    if (source != QStringLiteral("live")) return;

    QJsonObject payload;
    payload.insert(QStringLiteral("source"), source);
    payload.insert(QStringLiteral("level"), level.isEmpty() ? QStringLiteral("OK") : level);
    payload.insert(QStringLiteral("summary"), summary);
    payload.insert(QStringLiteral("diagnostics"), QJsonArray::fromVariantList(diagnostics));
    payload.insert(QStringLiteral("timing_rows"), QJsonArray::fromVariantList(timingRows));
    payload.insert(QStringLiteral("value_rows"), QJsonArray::fromVariantList(valueRows));
    payload.insert(QStringLiteral("alarm_rows"), QJsonArray::fromVariantList(alarmRows));
    payload.insert(QStringLiteral("timing_row_count"), timingRows.size());
    payload.insert(QStringLiteral("value_row_count"), valueRows.size());
    payload.insert(QStringLiteral("alarm_row_count"), alarmRows.size());

    QJsonObject counts;
    counts.insert(QStringLiteral("timing_rows"), timingRows.size());
    counts.insert(QStringLiteral("value_rows"), valueRows.size());
    counts.insert(QStringLiteral("alarm_rows"), alarmRows.size());
    const CoreViewSeverity severity = level == QStringLiteral("ERR")
        ? CoreViewSeverity::Error
        : (level == QStringLiteral("WARN") ? CoreViewSeverity::Warn : CoreViewSeverity::Ok);
    publishChange(m_viewStore.updateView(CoreViewName::AnalysisSnapshot, payload, severity, counts));
}

void CaptureCoreProcessRuntime::updateAnalysisStatus(quint64 queuedFrames,
                                                     quint64 maxQueuedFrames,
                                                     quint64 capacityFrames,
                                                     quint64 enqueuedFrames,
                                                     quint64 processedFrames,
                                                     quint64 overrunFrames,
                                                     quint64 pumpCount,
                                                     quint64 pumpMaxMs,
                                                     quint64 snapshotMaxMs,
                                                     quint64 truthLoss) {
    m_analysisTransportPayload = QJsonObject{{QStringLiteral("analysis_queue_frames"), QString::number(queuedFrames)},
                                             {QStringLiteral("analysis_max_queue_frames"), QString::number(maxQueuedFrames)},
                                             {QStringLiteral("analysis_capacity_frames"), QString::number(capacityFrames)},
                                             {QStringLiteral("analysis_enqueued_frames"), QString::number(enqueuedFrames)},
                                             {QStringLiteral("analysis_processed_frames"), QString::number(processedFrames)},
                                             {QStringLiteral("analysis_overrun_frames"), QString::number(overrunFrames)},
                                             {QStringLiteral("analysis_pump_count"), QString::number(pumpCount)},
                                             {QStringLiteral("analysis_pump_max_ms"), QString::number(pumpMaxMs)},
                                             {QStringLiteral("analysis_snapshot_max_ms"), QString::number(snapshotMaxMs)},
                                             {QStringLiteral("analysis_truth_loss"), QString::number(truthLoss)}};
    m_analysisTransportCheapCounts = QJsonObject{{QStringLiteral("analysis_queue_frames"), QString::number(queuedFrames)},
                                                 {QStringLiteral("analysis_overrun_frames"), QString::number(overrunFrames)},
                                                 {QStringLiteral("analysis_truth_loss"), QString::number(truthLoss)}};
    m_analysisSeverity = (overrunFrames > 0 || truthLoss > 0) ? CoreViewSeverity::Error : CoreViewSeverity::Ok;
    updateTransportSummary(QJsonObject{}, CoreViewSeverity::Ok, QJsonObject{});
}

void CaptureCoreProcessRuntime::queueRawLedgerFrames(const CanMonitorTransport::RawLedgerFrameBatch& batch) {
    if (batch.overrunFrames > 0) {
        m_rawLedgerDroppedHandoffFrames += batch.overrunFrames;
    }
    queueRawLedgerFrames(batch.frames);
}

void CaptureCoreProcessRuntime::queueRawLedgerFrames(const FrameRecordList& frames) {
    if (frames.isEmpty()) return;
    ensureRawLedgerRuntime();
    m_pendingRawLedgerFrames.reserve(m_pendingRawLedgerFrames.size() + frames.size());
    for (const FrameRecord& frame : frames) {
        m_pendingRawLedgerFrames.push_back(frame);
    }
    if (m_pendingRawLedgerFrames.size() > kCoreRawLedgerPendingFrameCap) {
        const int removeCount = m_pendingRawLedgerFrames.size() - kCoreRawLedgerPendingFrameCap;
        m_pendingRawLedgerFrames.erase(m_pendingRawLedgerFrames.begin(),
                                       m_pendingRawLedgerFrames.begin() + removeCount);
        m_rawLedgerDroppedHandoffFrames += quint64(removeCount);
    }
    flushRawLedgerFrames(false);
}

void CaptureCoreProcessRuntime::flushRawLedgerFrames(bool force) {
    if (!m_rawLedgerRuntime || m_pendingRawLedgerFrames.isEmpty()) return;
    if (m_rawLedgerDispatchInFlight && !force) return;

    FrameRecordList frames = std::move(m_pendingRawLedgerFrames);
    m_pendingRawLedgerFrames.clear();
    m_rawLedgerDispatchInFlight = true;
    QMetaObject::invokeMethod(m_rawLedgerRuntime,
                              [worker = QPointer<CanMonitorTransport::RawLedgerWriterRuntime>(m_rawLedgerRuntime),
                               frames = std::move(frames)]() mutable {
                                  if (worker) worker->appendFrames(std::move(frames));
                              },
                              force ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
    if (force) {
        m_rawLedgerDispatchInFlight = false;
    }
}

void CaptureCoreProcessRuntime::updateRawLedgerTailView(const FrameRecordList& frames,
                                                        quint64 firstSeq,
                                                        quint64 lastSeq,
                                                        quint64 totalRows,
                                                        quint64 segmentBytes,
                                                        const QString& path) {
    Q_UNUSED(lastSeq);
    for (int index = 0; index < frames.size(); ++index) {
        m_rawLedgerTailRows.append(frameToViewRow(frames.at(index), firstSeq + quint64(index)));
    }
    while (m_rawLedgerTailRows.size() > kCoreRawLedgerTailViewCap) {
        m_rawLedgerTailRows.removeAt(0);
        ++m_rawLedgerDroppedDisplayRows;
    }
    m_rawLedgerLastTotalRows = totalRows;
    m_rawLedgerLastSegmentBytes = segmentBytes;

    QJsonObject payload{{QStringLiteral("ok"), true},
                        {QStringLiteral("path"), path},
                        {QStringLiteral("first_seq"), QString::number(totalRows > quint64(m_rawLedgerTailRows.size())
                                                                          ? totalRows - quint64(m_rawLedgerTailRows.size())
                                                                          : 0)},
                        {QStringLiteral("last_seq"), QString::number(totalRows == 0 ? 0 : totalRows - 1)},
                        {QStringLiteral("total_rows"), QString::number(totalRows)},
                        {QStringLiteral("segment_bytes"), QString::number(segmentBytes)},
                        {QStringLiteral("frames"), m_rawLedgerTailRows},
                        {QStringLiteral("item_count"), m_rawLedgerTailRows.size()},
                        {QStringLiteral("item_cap"), kCoreRawLedgerTailViewCap},
                        {QStringLiteral("dropped_display_count"), QString::number(m_rawLedgerDroppedDisplayRows)},
                        {QStringLiteral("dropped_handoff_frames"), QString::number(m_rawLedgerDroppedHandoffFrames)}};
    addDecodedTailSemantics(payload);
    QJsonObject counts{{QStringLiteral("total_rows"), QString::number(totalRows)},
                       {QStringLiteral("segment_bytes"), QString::number(segmentBytes)},
                       {QStringLiteral("dropped_display_count"), QString::number(m_rawLedgerDroppedDisplayRows)},
                       {QStringLiteral("dropped_handoff_frames"), QString::number(m_rawLedgerDroppedHandoffFrames)}};
    const CoreViewSeverity severity = m_rawLedgerDroppedHandoffFrames > 0 ? CoreViewSeverity::Warn : CoreViewSeverity::Ok;
    publishChange(m_viewStore.updateView(CoreViewName::RawLedgerTail, payload, severity, counts));
}

void CaptureCoreProcessRuntime::updateRawLedgerStatusView(quint64 totalRows,
                                                          quint64 segmentBytes,
                                                          quint64 batchCount,
                                                          quint64 writeMaxUs,
                                                          quint64 writeFailures,
                                                          const QString& lastError) {
    m_rawLedgerLastTotalRows = totalRows;
    m_rawLedgerLastSegmentBytes = segmentBytes;
    m_rawLedgerLastBatchCount = batchCount;
    m_rawLedgerLastWriteMaxUs = writeMaxUs;
    m_rawLedgerLastWriteFailures = writeFailures;
    if (m_rawLedgerTailRows.isEmpty() && writeFailures == 0 && totalRows == 0) return;

    QJsonObject payload{{QStringLiteral("ok"), writeFailures == 0},
                        {QStringLiteral("total_rows"), QString::number(totalRows)},
                        {QStringLiteral("segment_bytes"), QString::number(segmentBytes)},
                        {QStringLiteral("batch_count"), QString::number(batchCount)},
                        {QStringLiteral("write_max_us"), QString::number(writeMaxUs)},
                        {QStringLiteral("write_failures"), QString::number(writeFailures)},
                        {QStringLiteral("frames"), m_rawLedgerTailRows},
                        {QStringLiteral("item_count"), m_rawLedgerTailRows.size()},
                        {QStringLiteral("item_cap"), kCoreRawLedgerTailViewCap},
                        {QStringLiteral("dropped_display_count"), QString::number(m_rawLedgerDroppedDisplayRows)},
                        {QStringLiteral("dropped_handoff_frames"), QString::number(m_rawLedgerDroppedHandoffFrames)}};
    addDecodedTailSemantics(payload);
    if (!lastError.isEmpty()) payload.insert(QStringLiteral("error"), lastError);
    QJsonObject counts{{QStringLiteral("total_rows"), QString::number(totalRows)},
                       {QStringLiteral("segment_bytes"), QString::number(segmentBytes)},
                       {QStringLiteral("write_failures"), QString::number(writeFailures)},
                       {QStringLiteral("dropped_handoff_frames"), QString::number(m_rawLedgerDroppedHandoffFrames)}};
    const CoreViewSeverity severity = writeFailures > 0
        ? CoreViewSeverity::Error
        : (m_rawLedgerDroppedHandoffFrames > 0 ? CoreViewSeverity::Warn : CoreViewSeverity::Ok);
    publishChange(m_viewStore.updateView(CoreViewName::RawLedgerTail, payload, severity, counts));
}

void CaptureCoreProcessRuntime::seedInitialViews() {
    m_viewStore.clear();
    m_rawLedgerTailRows = QJsonArray{};
    m_rawLedgerDroppedDisplayRows = 0;
    m_rawLedgerDroppedHandoffFrames = 0;
    m_rawLedgerLastTotalRows = 0;
    m_rawLedgerLastSegmentBytes = 0;
    m_lastRawIngressOverrunBytes = 0;
    m_captureProgressPath.clear();
    m_captureProgressBytesWritten = 0;
    m_captureProgressRecordCount = 0;
    m_captureProgressActive = false;
    m_captureProgressInvalid = false;
    m_captureProgressInvalidReason.clear();
    m_captureProgressRawIngressOverrunBytes = 0;
    m_fatalDiagnosticCount = 0;
    m_lastFatalDiagnosticCode.clear();
    m_lastFatalDiagnosticMessage.clear();
    m_lastFatalDiagnosticDetails = QJsonObject{};
    m_pipelineTransportPayload = QJsonObject{};
    m_pipelineTransportCheapCounts = QJsonObject{};
    m_analysisTransportPayload = QJsonObject{};
    m_analysisTransportCheapCounts = QJsonObject{};
    m_pipelineTransportSeverity = CoreViewSeverity::Ok;
    m_analysisSeverity = CoreViewSeverity::Ok;
    updateCoreHealth(QStringLiteral("ready"));
    updateTransportSummary(QJsonObject{{QStringLiteral("transport"), QStringLiteral("idle")},
                                       {QStringLiteral("serial_owner"), QStringLiteral("core")},
                                       {QStringLiteral("capture_active"), false}},
                           CoreViewSeverity::Ok,
                           QJsonObject{{QStringLiteral("transport"), QStringLiteral("idle")}});
    publishChange(m_viewStore.updateView(CoreViewName::RawLedgerTail,
                                         QJsonObject{{QStringLiteral("ok"), true},
                                                     {QStringLiteral("total_rows"), QStringLiteral("0")},
                                                     {QStringLiteral("segment_bytes"), QStringLiteral("0")},
                                                     {QStringLiteral("frames"), QJsonArray{}},
                                                     {QStringLiteral("item_count"), 0},
                                                     {QStringLiteral("item_cap"), kCoreRawLedgerTailViewCap},
                                                     {QStringLiteral("source"), QStringLiteral("decoded_can_tail_view")},
                                                     {QStringLiteral("evidence_kind"), QStringLiteral("decoded_can_tail")},
                                                     {QStringLiteral("truth_source"), QStringLiteral("capture.stream/index")},
                                                     {QStringLiteral("truth_semantics"), QStringLiteral("derived_display_tail_not_raw_evidence")},
                                                     {QStringLiteral("drop_semantics"), QStringLiteral("display_tail_drop_not_capture_truth_loss")}},
                                         CoreViewSeverity::Ok,
                                         QJsonObject{{QStringLiteral("total_rows"), QStringLiteral("0")},
                                                     {QStringLiteral("segment_bytes"), QStringLiteral("0")}}));
    publishChange(m_viewStore.updateView(CoreViewName::FatalDiagnostics,
                                         QJsonObject{{QStringLiteral("fatal_count"), QStringLiteral("0")},
                                                     {QStringLiteral("last_code"), QString()},
                                                     {QStringLiteral("last_message"), QString()}},
                                         CoreViewSeverity::Ok,
                                         QJsonObject{{QStringLiteral("fatal_count"), QStringLiteral("0")}}));
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
    QJsonObject mergedPayload = m_pipelineTransportPayload;
    for (auto it = m_analysisTransportPayload.constBegin(); it != m_analysisTransportPayload.constEnd(); ++it) {
        mergedPayload.insert(it.key(), it.value());
    }
    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
        mergedPayload.insert(it.key(), it.value());
    }
    if (!mergedPayload.contains(QStringLiteral("transport"))) {
        mergedPayload.insert(QStringLiteral("transport"),
                             m_transportConnected
                                 ? QStringLiteral("connected")
                                 : (m_transportRuntimeStarted ? QStringLiteral("idle") : QStringLiteral("idle")));
    }
    if (!mergedPayload.contains(QStringLiteral("message")) && !m_transportMessage.isEmpty()) {
        mergedPayload.insert(QStringLiteral("message"), m_transportMessage);
    }
    mergedPayload.insert(QStringLiteral("serial_owner"), QStringLiteral("core"));

    QJsonObject mergedCounts = m_pipelineTransportCheapCounts;
    for (auto it = m_analysisTransportCheapCounts.constBegin(); it != m_analysisTransportCheapCounts.constEnd(); ++it) {
        mergedCounts.insert(it.key(), it.value());
    }
    for (auto it = cheapCounts.constBegin(); it != cheapCounts.constEnd(); ++it) {
        mergedCounts.insert(it.key(), it.value());
    }
    publishChange(m_viewStore.updateView(CoreViewName::TransportSummary,
                                         mergedPayload,
                                         maxSeverity(maxSeverity(severity, m_pipelineTransportSeverity), m_analysisSeverity),
                                         mergedCounts));
}

void CaptureCoreProcessRuntime::updatePipelineTransportSummary(const QJsonObject& payload,
                                                               CoreViewSeverity severity,
                                                               const QJsonObject& cheapCounts) {
    m_pipelineTransportPayload = payload;
    m_pipelineTransportPayload.remove(QStringLiteral("transport"));
    m_pipelineTransportPayload.remove(QStringLiteral("message"));
    m_pipelineTransportPayload.remove(QStringLiteral("serial_owner"));
    m_pipelineTransportPayload.remove(QStringLiteral("capture_active"));
    m_pipelineTransportCheapCounts = cheapCounts;
    m_pipelineTransportSeverity = severity;
    updateTransportSummary(QJsonObject{}, CoreViewSeverity::Ok, QJsonObject{});
}

void CaptureCoreProcessRuntime::handleAnalysisModelRequested(quint64 requestId, const QString& modelPath, bool modelEnabled) {
    Q_UNUSED(requestId);
    ensureAnalysisRuntime();

    CanMonitorAnalysis::AnalysisRuntime::Config config;
    config.modelEnabled = false;
    config.maxStateKeys = 8192;
    config.maxRowsPerSnapshot = 1600;

    if (modelEnabled && !modelPath.trimmed().isEmpty()) {
        CanModel::ModelPack pack;
        QString error;
        if (CanModel::ModelPackLoader::loadFile(modelPath, &pack, &error)) {
            config.modelEnabled = true;
            config.rules = pack.rules;
            config.signalMessages = pack.messages;
            updateCoreHealth(QStringLiteral("analysis_model_loaded"));
        } else {
            emit errorOccurred(QStringLiteral("core analysis model load failed: %1").arg(error));
            publishChange(m_viewStore.updateView(CoreViewName::AnalysisSnapshot,
                                                 QJsonObject{{QStringLiteral("source"), QStringLiteral("live")},
                                                             {QStringLiteral("level"), QStringLiteral("WARN")},
                                                             {QStringLiteral("summary"), QStringLiteral("core analysis model unavailable")},
                                                             {QStringLiteral("error"), error},
                                                             {QStringLiteral("diagnostics"), QJsonArray{}},
                                                             {QStringLiteral("timing_rows"), QJsonArray{}},
                                                             {QStringLiteral("value_rows"), QJsonArray{}},
                                                             {QStringLiteral("alarm_rows"), QJsonArray{}}},
                                                 CoreViewSeverity::Warn,
                                                 QJsonObject{{QStringLiteral("model_error"), true}}));
        }
    }

    QMetaObject::invokeMethod(m_analysisRuntime,
                              [worker = QPointer<CanMonitorAnalysis::AnalysisWorkerRuntime>(m_analysisRuntime),
                               config]() {
                                  if (worker) worker->setConfig(config);
                              },
                              Qt::QueuedConnection);
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

    m_pendingPipelineMirrorViews.insert(static_cast<int>(viewName), viewName);
    if (m_pipelineMirrorFlushScheduled) return;
    m_pipelineMirrorFlushScheduled = true;
    QTimer::singleShot(50, this, &CaptureCoreProcessRuntime::flushPipelineViewMirrors);
}

void CaptureCoreProcessRuntime::flushPipelineViewMirrors() {
    m_pipelineMirrorFlushScheduled = false;
    if (!m_pipelineRuntime) {
        m_pendingPipelineMirrorViews.clear();
        return;
    }

    const auto pendingViews = m_pendingPipelineMirrorViews;
    m_pendingPipelineMirrorViews.clear();
    for (auto it = pendingViews.cbegin(); it != pendingViews.cend(); ++it) {
        const CoreViewName viewName = it.value();
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
    if (!m_pendingPipelineMirrorViews.isEmpty() && !m_pipelineMirrorFlushScheduled) {
        m_pipelineMirrorFlushScheduled = true;
        QTimer::singleShot(50, this, &CaptureCoreProcessRuntime::flushPipelineViewMirrors);
    }
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
    if (viewName == CoreViewName::TransportSummary) {
        updatePipelineTransportSummary(payload, severityFromJson(snapshot), cheapCounts);
        return;
    }
    if (viewName == CoreViewName::CaptureProgress) {
        if (payload.value(QStringLiteral("capture_handoff_overrun")).toBool(false)) {
            latchCaptureInvalid(payload.value(QStringLiteral("capture_handoff_error"))
                                    .toString(QStringLiteral("capture handoff overrun")));
            publishFatalDiagnostic(QStringLiteral("capture_handoff_overrun"),
                                   QStringLiteral("Capture handoff overrun before writer"),
                                   QJsonObject{{QStringLiteral("overrun_records"), payload.value(QStringLiteral("capture_handoff_overrun_records"))},
                                               {QStringLiteral("overrun_bytes"), payload.value(QStringLiteral("capture_handoff_overrun_bytes"))}});
            CanMonitorTransport::TypedCaptureWriterRuntime::Status status;
            status.captureInvalid = true;
            updateCaptureProgressView(status);
        }
        return;
    }
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
    sendCoreHostFrame(requestId, frame, summary);
}

void CaptureCoreProcessRuntime::sendCoreHostFrame(quint64 requestId, const QByteArray& frame, const QString& summary) {
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

void CaptureCoreProcessRuntime::handleControlCycleRequested(quint64, const QString& action, const QJsonObject& payload) {
    if (action == QStringLiteral("start")) {
        startControlCycle(payload);
    } else if (action == QStringLiteral("update")) {
        updateControlCycle(payload);
    } else if (action == QStringLiteral("stop")) {
        stopControlCycle();
    } else if (action == QStringLiteral("burst_once")) {
        sendControlCycleBurstOnce(payload);
    }
}

void CaptureCoreProcessRuntime::startControlCycle(const QJsonObject& payload) {
    const int clampedPeriodMs = m_controlCycle.start(payload.value(QStringLiteral("signed_command")).toInt(),
                                                     payload.value(QStringLiteral("rpm")).toInt(),
                                                     payload.value(QStringLiteral("steering_deg")).toDouble(),
                                                     quint8(payload.value(QStringLiteral("motor_mode")).toInt(1)),
                                                     quint8(payload.value(QStringLiteral("driving_mode")).toInt(1)),
                                                     quint8(payload.value(QStringLiteral("bus")).toInt()),
                                                     payload.value(QStringLiteral("period_ms")).toInt(20),
                                                     payload.value(QStringLiteral("frame_gap_ms")).toInt(2));
    if (m_controlCycleTimerId != 0) killTimer(m_controlCycleTimerId);
    m_controlCycleTimerId = startTimer(clampedPeriodMs, Qt::PreciseTimer);
    beginControlCycle();
}

void CaptureCoreProcessRuntime::updateControlCycle(const QJsonObject& payload) {
    m_controlCycle.update(payload.value(QStringLiteral("signed_command")).toInt(),
                          payload.value(QStringLiteral("rpm")).toInt(),
                          payload.value(QStringLiteral("steering_deg")).toDouble(),
                          quint8(payload.value(QStringLiteral("motor_mode")).toInt(1)),
                          quint8(payload.value(QStringLiteral("driving_mode")).toInt(1)),
                          quint8(payload.value(QStringLiteral("bus")).toInt()));
}

void CaptureCoreProcessRuntime::stopControlCycle() {
    m_controlCycle.stop();
    if (m_controlCycleTimerId != 0) {
        killTimer(m_controlCycleTimerId);
        m_controlCycleTimerId = 0;
    }
    if (m_controlCycleGapTimerId != 0) {
        killTimer(m_controlCycleGapTimerId);
        m_controlCycleGapTimerId = 0;
    }
}

void CaptureCoreProcessRuntime::sendControlCycleBurstOnce(const QJsonObject& payload) {
    dispatchControlCycleResult(m_controlCycle.burstOnce(payload.value(QStringLiteral("signed_command")).toInt(),
                                                        payload.value(QStringLiteral("rpm")).toInt(),
                                                        payload.value(QStringLiteral("steering_deg")).toDouble(),
                                                        quint8(payload.value(QStringLiteral("motor_mode")).toInt(1)),
                                                        quint8(payload.value(QStringLiteral("driving_mode")).toInt(1)),
                                                        quint8(payload.value(QStringLiteral("bus")).toInt()),
                                                        payload.value(QStringLiteral("reason")).toString(),
                                                        payload.value(QStringLiteral("reset_slew")).toBool()));
}

void CaptureCoreProcessRuntime::beginControlCycle() {
    dispatchControlCycleResult(m_controlCycle.beginCycle());
}

void CaptureCoreProcessRuntime::continueControlCycleBurst() {
    dispatchControlCycleResult(m_controlCycle.continuePacedBurst());
}

void CaptureCoreProcessRuntime::dispatchControlCycleResult(const CanMonitorControl::ControlCycleRuntime::CycleResult& result) {
    for (const QString& error : result.errors) {
        emit errorOccurred(error);
    }
    for (const auto& frame : result.frames) {
        sendCoreHostFrame(0, frame.frame, frame.summary);
    }
    if (result.scheduleGap) {
        if (m_controlCycleGapTimerId != 0) killTimer(m_controlCycleGapTimerId);
        m_controlCycleGapTimerId = startTimer(result.gapMs, Qt::PreciseTimer);
    }
}

void CaptureCoreProcessRuntime::handleCaptureStartRequested(quint64 requestId,
                                                            const QString& sessionDir,
                                                            const QJsonObject& metadata) {
    ensureTransportRuntime();
    ensureCaptureWriterRuntime();
    if (m_captureQueue) m_captureQueue->clear();
    m_captureProgressPath = sessionDir;
    m_captureProgressBytesWritten = 0;
    m_captureProgressRecordCount = 0;
    m_captureProgressActive = false;
    m_captureProgressInvalid = false;
    m_captureProgressInvalidReason.clear();
    m_captureProgressRawIngressOverrunBytes = 0;

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

void CaptureCoreProcessRuntime::latchCaptureInvalid(const QString& reason, quint64 rawIngressOverrunBytes) {
    m_captureProgressInvalid = true;
    if (!reason.isEmpty()) {
        m_captureProgressInvalidReason = reason;
    }
    if (rawIngressOverrunBytes > 0) {
        m_captureProgressRawIngressOverrunBytes = std::max(m_captureProgressRawIngressOverrunBytes, rawIngressOverrunBytes);
    }
}

void CaptureCoreProcessRuntime::publishFatalDiagnostic(const QString& code,
                                                       const QString& message,
                                                       const QJsonObject& details) {
    ++m_fatalDiagnosticCount;
    m_lastFatalDiagnosticCode = code;
    m_lastFatalDiagnosticMessage = message;
    m_lastFatalDiagnosticDetails = details;

    QJsonObject payload;
    payload.insert(QStringLiteral("fatal_count"), QString::number(m_fatalDiagnosticCount));
    payload.insert(QStringLiteral("last_code"), m_lastFatalDiagnosticCode);
    payload.insert(QStringLiteral("last_message"), m_lastFatalDiagnosticMessage);
    payload.insert(QStringLiteral("last_details"), m_lastFatalDiagnosticDetails);

    QJsonObject counts;
    counts.insert(QStringLiteral("fatal_count"), QString::number(m_fatalDiagnosticCount));
    counts.insert(QStringLiteral("last_code"), m_lastFatalDiagnosticCode);

    publishChange(m_viewStore.updateView(CoreViewName::FatalDiagnostics,
                                         payload,
                                         CoreViewSeverity::Fatal,
                                         counts));
}

void CaptureCoreProcessRuntime::updateCaptureProgressView(
    const CanMonitorTransport::TypedCaptureWriterRuntime::Status& status,
    const CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate* update) {
    m_captureProgressActive = status.active;
    if (status.captureInvalid || status.overrunBytes > 0 || status.overrunRecords > 0) {
        latchCaptureInvalid(QStringLiteral("capture writer reported invalid or overrun"));
    }
    if (update) {
        m_captureProgressActive = update->active;
        if (!update->path.isEmpty()) m_captureProgressPath = update->path;
        if (update->stateChanged && update->active && update->bytesWritten == 0 && update->recordCount == 0) {
            m_captureProgressBytesWritten = 0;
            m_captureProgressRecordCount = 0;
        } else {
            m_captureProgressBytesWritten = std::max(m_captureProgressBytesWritten, update->bytesWritten);
            m_captureProgressRecordCount = std::max(m_captureProgressRecordCount, update->recordCount);
        }
        if (update->stateChanged && !update->active) {
            m_captureProgressActive = false;
        }
        if (!update->ok) {
            latchCaptureInvalid(update->error.isEmpty() ? QStringLiteral("capture storage update failed") : update->error);
        }
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("capture_active"), m_captureProgressActive);
    payload.insert(QStringLiteral("capture_invalid"), m_captureProgressInvalid);
    payload.insert(QStringLiteral("capture_invalid_reason"), m_captureProgressInvalidReason);
    payload.insert(QStringLiteral("raw_ingress_overrun_bytes"), QString::number(m_captureProgressRawIngressOverrunBytes));
    payload.insert(QStringLiteral("path"), m_captureProgressPath);
    payload.insert(QStringLiteral("bytes_written"), QString::number(m_captureProgressBytesWritten));
    payload.insert(QStringLiteral("record_count"), QString::number(m_captureProgressRecordCount));
    payload.insert(QStringLiteral("queued_records"), QString::number(status.queuedRecords));
    payload.insert(QStringLiteral("queued_bytes"), QString::number(status.queuedBytes));
    payload.insert(QStringLiteral("max_queued_bytes"), QString::number(status.maxQueuedBytes));
    payload.insert(QStringLiteral("overrun_records"), QString::number(status.overrunRecords));
    payload.insert(QStringLiteral("overrun_bytes"), QString::number(status.overrunBytes));
    payload.insert(QStringLiteral("write_max_ms"), QString::number(status.writeMaxMs));
    if (update) {
        payload.insert(QStringLiteral("ok"), update->ok);
        payload.insert(QStringLiteral("state_changed"), update->stateChanged);
        if (!update->error.isEmpty()) payload.insert(QStringLiteral("error"), update->error);
    }

    QJsonObject counts;
    counts.insert(QStringLiteral("queued_bytes"), QString::number(status.queuedBytes));
    counts.insert(QStringLiteral("overrun_bytes"), QString::number(status.overrunBytes));
    counts.insert(QStringLiteral("raw_ingress_overrun_bytes"), QString::number(m_captureProgressRawIngressOverrunBytes));
    counts.insert(QStringLiteral("record_count"), QString::number(m_captureProgressRecordCount));

    publishChange(m_viewStore.updateView(CanMonitorCore::CoreViewName::CaptureProgress,
                                         payload,
                                         m_captureProgressInvalid
                                             ? CanMonitorCore::CoreViewSeverity::Error
                                             : CanMonitorCore::CoreViewSeverity::Ok,
                                         counts));
}

} // namespace CanMonitorCore
