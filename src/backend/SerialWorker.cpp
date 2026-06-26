#include "SerialWorker.h"

#include "AppLogging.h"
#include "perf/PerformanceProbeRuntime.h"

#include <QDateTime>
#include <QIODevice>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <cstring>

namespace {
constexpr int kTypedHandshakeWatchdogIntervalMs = 250;
constexpr int kTypedHandshakeTimeoutMs = 3500;
constexpr int kUiProjectionFlushIntervalMs = 250;
constexpr int kUiProjectionMaxFramesPerFlush = 4;
constexpr int kUiProjectionHardPendingKeys = 64;
constexpr int kRawLedgerFlushIntervalMs = 60;
constexpr int kRawLedgerMaxRecordsPerFlush = 512;
constexpr quint64 kRawLedgerHandoffMaxBytes = 16ULL * 1024ULL * 1024ULL;
constexpr qsizetype kDrainProcessMaxBytes = 512 * 1024;
constexpr int kDrainPipelineStatusIntervalMs = 250;
constexpr qsizetype kAnalysisHandoffMaxFrames = 131072;
constexpr qsizetype kAnalysisHandoffDispatchFrames = 8192;
constexpr quint64 kCaptureWriterHandoffMaxBytes = 16ULL * 1024ULL * 1024ULL;
constexpr int kCaptureWriterHandoffDispatchRecords = 4096;

FrameRecord typedCanToFrameRecord(const TypedRecord& record, const TypedCanRawRecord& can) {
    FrameRecord frame;
    frame.tExtUs = can.monoUs;
    frame.canId = can.canId;
    frame.ext = can.extended;
    frame.rtr = can.rtr;
    frame.dlc = can.dlc;
    frame.bus = can.bus;
    frame.seq = quint8(record.header.seq & 0xFF);
    std::memcpy(frame.data, can.data, sizeof(frame.data));
    return frame;
}

FrameRecord typedSegmentEntryToFrameRecord(const TypedRecord& record, const TypedCanRxSegmentEntry& entry) {
    FrameRecord frame;
    frame.tExtUs = entry.monoUs;
    frame.canId = entry.canId;
    frame.ext = entry.extended;
    frame.rtr = entry.rtr;
    frame.dlc = entry.dlc;
    frame.bus = entry.bus;
    frame.seq = quint8(record.header.seq & 0xFF);
    frame.hasCaptureSeq = true;
    frame.captureSeq = entry.captureSeq;
    std::memcpy(frame.data, entry.data, sizeof(frame.data));
    return frame;
}

template <typename Fn>
void forEachCanRxFrame(const TypedRecord& record, Fn&& fn) {
    if (record.isType(TypedRecordType::CanRxRaw)) {
        const auto can = decodeTypedCanRaw(record);
        if (can && !can->txAudit) fn(typedCanToFrameRecord(record, *can));
        return;
    }
    if (!record.isType(TypedRecordType::CanRxSegment)) return;
    const auto header = decodeTypedCanRxSegmentHeader(record);
    if (!header) return;
    for (qsizetype index = 0; index < header->frameCount; ++index) {
        const auto entry = decodeTypedCanRxSegmentEntry(record, index);
        if (entry) fn(typedSegmentEntryToFrameRecord(record, *entry));
    }
}

void mergeTrace(QJsonObject& target, const QJsonObject& source) {
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        target.insert(it.key(), it.value());
    }
}

QJsonObject serialLiveTraceJson(CanMonitorTransport::LivePathTelemetry& telemetry, qint64 nowMs) {
    QJsonObject out;
    CanMonitorTransport::insertCounter(out, QStringLiteral("projected_signal_received"), telemetry.projectedSignalReceived);
    CanMonitorTransport::insertCounter(out, QStringLiteral("projected_frames_received"), telemetry.projectedFramesReceived);
    CanMonitorTransport::insertCounter(out, QStringLiteral("projection_queue_keys"), telemetry.projectionQueueKeys);
    CanMonitorTransport::insertCounter(out, QStringLiteral("projection_flush_count"), telemetry.projectionFlushCount);
    CanMonitorTransport::insertCounter(out, QStringLiteral("frames_received_emit"), telemetry.framesReceivedEmit);
    out.insert(QStringLiteral("frames_received_emit_per_sec"),
               telemetry.rates.ratePerSec(QStringLiteral("frames_received_emit"), telemetry.framesReceivedEmit, nowMs));
    CanMonitorTransport::insertCounter(out, QStringLiteral("frames_received_frames"), telemetry.framesReceivedFrames);
    CanMonitorTransport::insertCounter(out, QStringLiteral("frames_received_emit_seq"), telemetry.framesReceivedEmitSeq);
    CanMonitorTransport::insertCounter(out, QStringLiteral("frames_received_last_emit_wall_ms"),
                                       quint64(std::max<qint64>(0, telemetry.framesReceivedLastEmitWallMs)));
    return out;
}

QJsonObject serialDrainTraceJson(CanMonitorTransport::DrainEventTelemetry& telemetry, qint64 nowMs) {
    QJsonObject out;
    CanMonitorTransport::insertCounter(out, QStringLiteral("scheduleDrainPump_calls"), telemetry.scheduleDrainPumpCalls);
    out.insert(QStringLiteral("scheduleDrainPump_per_sec"),
               telemetry.rates.ratePerSec(QStringLiteral("scheduleDrainPump_calls"), telemetry.scheduleDrainPumpCalls, nowMs));
    CanMonitorTransport::insertCounter(out, QStringLiteral("typed_worker_invoke_requests"), telemetry.typedWorkerInvokeRequests);
    CanMonitorTransport::insertCounter(out, QStringLiteral("typed_worker_invoke_suppressed"), telemetry.typedWorkerInvokeSuppressed);
    CanMonitorTransport::insertCounter(out, QStringLiteral("drain_status_received"), telemetry.drainStatusReceived);
    out.insert(QStringLiteral("drain_pump_scheduled_flag"), telemetry.drainPumpScheduledFlag);
    CanMonitorTransport::insertCounter(out, QStringLiteral("typedProjectionStatus_emit"), telemetry.typedProjectionStatusEmit);
    CanMonitorTransport::insertCounter(out, QStringLiteral("typedProjectionStatus_receive"), telemetry.typedProjectionStatusReceive);
    CanMonitorTransport::insertCounter(out, QStringLiteral("typedTruthStatus_emit"), telemetry.typedTruthStatusEmit);
    CanMonitorTransport::insertCounter(out, QStringLiteral("typedTruthStatus_receive"), telemetry.typedTruthStatusReceive);
    CanMonitorTransport::insertCounter(out, QStringLiteral("typedTransportStatus_emit"), telemetry.typedTransportStatusEmit);
    CanMonitorTransport::insertCounter(out, QStringLiteral("typedTransportStatus_receive"), telemetry.typedTransportStatusReceive);
    CanMonitorTransport::insertCounter(out, QStringLiteral("analysis_handoff_pending_frames"), telemetry.analysisHandoffPendingFrames);
    CanMonitorTransport::insertCounter(out, QStringLiteral("analysis_handoff_max_pending_frames"), telemetry.analysisHandoffMaxPendingFrames);
    CanMonitorTransport::insertCounter(out, QStringLiteral("analysis_handoff_dispatch_count"), telemetry.analysisHandoffDispatchCount);
    CanMonitorTransport::insertCounter(out, QStringLiteral("analysis_handoff_dispatch_frames"), telemetry.analysisHandoffDispatchFrames);
    CanMonitorTransport::insertCounter(out, QStringLiteral("analysis_handoff_complete_count"), telemetry.analysisHandoffCompleteCount);
    CanMonitorTransport::insertCounter(out, QStringLiteral("analysis_handoff_complete_frames"), telemetry.analysisHandoffCompleteFrames);
    CanMonitorTransport::insertCounter(out, QStringLiteral("analysis_handoff_overrun_frames"), telemetry.analysisHandoffOverrunFrames);
    out.insert(QStringLiteral("analysis_handoff_inflight"), telemetry.analysisHandoffInflight);
    CanMonitorTransport::insertCounter(out, QStringLiteral("raw_ledger_handoff_pending_frames"), telemetry.rawLedgerHandoffPendingFrames);
    CanMonitorTransport::insertCounter(out, QStringLiteral("raw_ledger_handoff_pending_bytes"), telemetry.rawLedgerHandoffPendingBytes);
    CanMonitorTransport::insertCounter(out, QStringLiteral("raw_ledger_handoff_max_pending_bytes"), telemetry.rawLedgerHandoffMaxPendingBytes);
    CanMonitorTransport::insertCounter(out, QStringLiteral("raw_ledger_handoff_dispatch_count"), telemetry.rawLedgerHandoffDispatchCount);
    CanMonitorTransport::insertCounter(out, QStringLiteral("raw_ledger_handoff_dispatch_frames"), telemetry.rawLedgerHandoffDispatchFrames);
    CanMonitorTransport::insertCounter(out, QStringLiteral("raw_ledger_handoff_complete_count"), telemetry.rawLedgerHandoffCompleteCount);
    CanMonitorTransport::insertCounter(out, QStringLiteral("raw_ledger_handoff_complete_frames"), telemetry.rawLedgerHandoffCompleteFrames);
    CanMonitorTransport::insertCounter(out, QStringLiteral("raw_ledger_handoff_overrun_bytes"), telemetry.rawLedgerHandoffOverrunBytes);
    out.insert(QStringLiteral("raw_ledger_handoff_inflight"), telemetry.rawLedgerHandoffInflight);
    CanMonitorTransport::insertCounter(out, QStringLiteral("truth_handoff_emit_count"), telemetry.truthHandoffEmitCount);
    CanMonitorTransport::insertCounter(out, QStringLiteral("truth_handoff_emit_frames"), telemetry.truthHandoffEmitFrames);
    CanMonitorTransport::insertCounter(out, QStringLiteral("truth_handoff_pending_keys"), telemetry.truthHandoffPendingKeys);
    CanMonitorTransport::insertCounter(out, QStringLiteral("truth_handoff_flush_count"), telemetry.truthHandoffFlushCount);
    return out;
}

}

SerialWorker::SerialWorker(QObject* parent)
    : QObject(parent),
      m_legacyIngress(this),
      m_drainQueue(new CanMonitorTransport::DrainByteQueue()),
      m_captureRecordQueue(new CanMonitorTransport::TypedRecordHandoffQueue()) {}

SerialWorker::~SerialWorker() {
    finalizeCaptureWriterIfActive();
    flushRawLedgerHandoffSync();
    shutdownDrainRuntime();
    shutdownTypedPipelineRuntime();
    shutdownAnalysisRuntime();
    shutdownCaptureWriterRuntime();
    shutdownRawLedgerRuntime();
}

void SerialWorker::start(const QString& portName) {
    stop();

    m_typedPipeline.reset();
    const QString endpoint = portName.trimmed();
    m_legacyIngress.resetStreamState();
    resetProjectionQueue();
    m_drainEventTelemetry = CanMonitorTransport::DrainEventTelemetry{};
    m_drainRuntimeEventTrace = {};
    m_pipelineLivePathTrace = {};
    m_pipelineDrainEventTrace = {};
    ensureRawLedgerRuntime();
    resetRawLedger(QStringLiteral("live"));
    ensureDrainRuntime();
    ensureTypedPipelineRuntime();
    if (endpoint.startsWith(QStringLiteral("tcp://"), Qt::CaseInsensitive)) {
        startGatewayTcp(endpoint);
        return;
    }
    if (m_transportMode == TransportMode::TypedEvidence) {
        startTypedHandshakeWatchdog();
    }
    QMetaObject::invokeMethod(m_drainRuntime, [runtime = m_drainRuntime, endpoint]() {
        runtime->startSerial(endpoint);
    }, Qt::QueuedConnection);
}

void SerialWorker::stop() {
    if (m_controlCycle.enabled() && m_connected) {
        dispatchControlCycleResult(m_controlCycle.burstOnce(0,
                                                            0,
                                                            0.0,
                                                            1,
                                                            1,
                                                            m_controlCycle.bus(),
                                                            QStringLiteral("serial stop safety neutral"),
                                                            true,
                                                            false));
    }
    stopControlCycle();
    stopTypedHandshakeWatchdog();
    emitLegacyLoggingUpdate(m_legacyIngress.stopLoggingIfActive());
    emitTypedStorageUpdate(finalizeCaptureWriterIfActive());
    flushRawLedgerHandoffSync();
    closeSerialPortForRecovery(QStringLiteral("operator disconnect"));
    emit stateChanged(false, QStringLiteral("연결 해제"));
}

void SerialWorker::resetRawLedger(const QString& label) {
    ensureRawLedgerRuntime();
    flushRawLedgerHandoffSync();
    m_pendingRawLedgerFrames.clear();
    m_pendingRawLedgerBytes = 0;
    m_pendingRawLedgerMaxBytes = 0;
    m_rawLedgerHandoffOverrunBytes = 0;
    m_rawLedgerWriteMaxUs = 0;
    m_rawLedgerWriteFailures = 0;
    m_rawLedgerLastError.clear();
    const QString normalized = label.trimmed().isEmpty() ? QStringLiteral("live") : label.trimmed();
    QMetaObject::invokeMethod(m_rawLedgerWorker, [worker = m_rawLedgerWorker, normalized]() {
        worker->reset(normalized);
    }, Qt::BlockingQueuedConnection);
}

bool SerialWorker::setTypedStorage(bool enable, const QString& sessionDir, const QJsonObject& metadata) {
    ensureCaptureWriterRuntime();
    if (enable) {
        if (m_transportMode != TransportMode::TypedEvidence) {
            emit errorOccurred(QStringLiteral("Typed storage requires TypedEvidence transport mode."));
            return false;
        }
        m_pendingCaptureWriterRecords.clear();
        if (m_captureRecordQueue) m_captureRecordQueue->clear();
        m_pendingCaptureWriterBytes = 0;
        m_pendingCaptureWriterMaxBytes = 0;
        m_captureWriterHandoffOverrunBytes = 0;
        m_captureWriterDispatchScheduled = false;
        m_captureWriterDispatchInFlight = false;
        CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate update;
        QMetaObject::invokeMethod(m_captureWriterWorker, [this, sessionDir, metadata, &update]() {
            update = m_captureWriterWorker->startStorageSync(sessionDir, metadata);
        }, Qt::BlockingQueuedConnection);
        m_typedCaptureEnabled = update.ok;
        if (m_typedPipelineWorker) {
            QMetaObject::invokeMethod(m_typedPipelineWorker,
                                      [worker = QPointer<CanMonitorTransport::TypedEvidencePipelineWorkerRuntime>(m_typedPipelineWorker), enabled = m_typedCaptureEnabled]() {
                                          if (worker) worker->setCaptureEnabled(enabled);
                                      },
                                      Qt::BlockingQueuedConnection);
        }
        emitTypedStorageUpdate(update);
        return update.ok;
    }

    m_typedCaptureEnabled = false;
    if (m_typedPipelineWorker) {
        QMetaObject::invokeMethod(m_typedPipelineWorker,
                                  [worker = QPointer<CanMonitorTransport::TypedEvidencePipelineWorkerRuntime>(m_typedPipelineWorker)]() {
                                      if (worker) worker->setCaptureEnabled(false);
                                  },
                                  Qt::BlockingQueuedConnection);
    }
    flushCaptureWriterHandoffSync();
    CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate update;
    QJsonObject diagnostics = m_pipelineCaptureDiagnostics.isEmpty()
        ? m_typedPipeline.makeCaptureDiagnostics()
        : m_pipelineCaptureDiagnostics;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QJsonObject liveTrace = m_pipelineLivePathTrace;
    mergeTrace(liveTrace, serialLiveTraceJson(m_livePathTelemetry, nowMs));
    QJsonObject drainTrace = m_drainRuntimeEventTrace;
    mergeTrace(drainTrace, m_pipelineDrainEventTrace);
    mergeTrace(drainTrace, serialDrainTraceJson(m_drainEventTelemetry, nowMs));
    diagnostics.insert(QStringLiteral("live_path_trace"), liveTrace);
    diagnostics.insert(QStringLiteral("drain_event_trace"), drainTrace);
    QMetaObject::invokeMethod(m_captureWriterWorker, [this, sessionDir, diagnostics, &update]() {
        update = m_captureWriterWorker->stopStorageSync(sessionDir, diagnostics);
    }, Qt::BlockingQueuedConnection);
    emitTypedStorageUpdate(update);
    return update.ok;
}

void SerialWorker::sendHostFrame(const QByteArray& frame, const QString& summary) {
    if (frame.isEmpty()) {
        emit hostFrameWriteResult(false, QStringLiteral("empty host frame"), 0);
        return;
    }
    if (!m_connected || !m_drainRuntime) {
        emit hostFrameWriteResult(false, summary.isEmpty() ? QStringLiteral("transport not connected") : summary, 0);
        return;
    }
    QMetaObject::invokeMethod(m_drainRuntime, [runtime = m_drainRuntime, frame, summary]() {
        runtime->sendHostFrame(frame, summary);
    }, Qt::QueuedConnection);
}

void SerialWorker::startControlCycle(int signedCommand,
                                     int rpm,
                                     double steeringDeg,
                                     quint8 motorMode,
                                     quint8 drivingMode,
                                     quint8 bus,
                                     int periodMs,
                                     int frameGapMs) {
    const int clampedPeriodMs = m_controlCycle.start(signedCommand,
                                                     rpm,
                                                     steeringDeg,
                                                     motorMode,
                                                     drivingMode,
                                                     bus,
                                                     periodMs,
                                                     frameGapMs);
    if (m_controlCycleTimerId != 0) killTimer(m_controlCycleTimerId);
    m_controlCycleTimerId = startTimer(clampedPeriodMs, Qt::PreciseTimer);
    beginControlCycle();
}

void SerialWorker::updateControlCycle(int signedCommand,
                                      int rpm,
                                      double steeringDeg,
                                      quint8 motorMode,
                                      quint8 drivingMode,
                                      quint8 bus) {
    m_controlCycle.update(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus);
}

void SerialWorker::stopControlCycle() {
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

void SerialWorker::sendControlCycleBurstOnce(int signedCommand,
                                             int rpm,
                                             double steeringDeg,
                                             quint8 motorMode,
                                             quint8 drivingMode,
                                             quint8 bus,
                                             const QString& reason,
                                             bool resetSlew) {
    dispatchControlCycleResult(m_controlCycle.burstOnce(signedCommand,
                                                        rpm,
                                                        steeringDeg,
                                                        motorMode,
                                                        drivingMode,
                                                        bus,
                                                        reason,
                                                        resetSlew));
}

void SerialWorker::setLogging(bool enable, const QString& binPath, const QString& metaPath, const QString& rulesSnapshotPath, const QString& rulesSourcePath) {
    if (m_transportMode == TransportMode::TypedEvidence) {
        if (enable) {
            emit errorOccurred(QStringLiteral("Typed evidence mode uses typed capture storage; legacy 20B logging is disabled."));
        }
        return;
    }

    emitLegacyLoggingUpdate(enable
        ? m_legacyIngress.startLogging(binPath, metaPath, rulesSnapshotPath, rulesSourcePath)
        : m_legacyIngress.stopLogging());
}

void SerialWorker::onReadyRead() {
    QIODevice* device = activeDevice();
    if (!device) return;
    const QByteArray bytes = device->readAll();
    if (!bytes.isEmpty()) processIncomingBytes(bytes);
}

void SerialWorker::onBytesWritten(qint64 bytes) {
    Q_UNUSED(bytes);
    drainHostTxQueue();
}

void SerialWorker::timerEvent(QTimerEvent* event) {
    if (event->timerId() == m_typedHandshakeTimerId) {
        const qint64 elapsedMs = m_typedHandshakeClock.isValid() ? m_typedHandshakeClock.elapsed() : -1;
        const auto handshake = m_typedPipeline.evaluateHandshake(elapsedMs, kTypedHandshakeTimeoutMs);
        if (handshake.capabilitySeen) {
            stopTypedHandshakeWatchdog();
            return;
        }
        if (handshake.timedOut) {
            qCWarning(logTransport).noquote() << handshake.timeoutReason;
            emit errorOccurred(handshake.timeoutReason);
            closeSerialPortForRecovery(handshake.timeoutReason);
            clearHostTxQueue(handshake.timeoutReason);
            emit stateChanged(false, QStringLiteral("Typed evidence handshake timeout; port closed"));
            return;
        }
        return;
    }
    if (event->timerId() == m_projectionFlushTimerId) {
        flushQueuedProjectionFrames(false);
        if (m_pendingProjectionFramesByKey.isEmpty()) {
            killTimer(m_projectionFlushTimerId);
            m_projectionFlushTimerId = 0;
            m_projectionFlushClock.invalidate();
        }
        return;
    }
    if (event->timerId() == m_rawLedgerFlushTimerId) {
        flushQueuedRawLedgerRecords(false);
        if (m_pendingRawLedgerFrames.isEmpty()) {
            killTimer(m_rawLedgerFlushTimerId);
            m_rawLedgerFlushTimerId = 0;
        }
        return;
    }
    if (event->timerId() == m_truthFlushTimerId) {
        flushQueuedTruthFrames(false);
        if (!m_liveTruth.hasPending()) {
            killTimer(m_truthFlushTimerId);
            m_truthFlushTimerId = 0;
        }
        return;
    }
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

void SerialWorker::setTransportMode(TransportMode mode) {
    if (m_transportMode == mode) return;
    m_transportMode = mode;
    m_legacyIngress.resetStreamState();
    m_typedPipeline.reset();
    m_pipelineStatus = {};
    m_pipelineCaptureDiagnostics = {};
    if (m_typedPipelineWorker) {
        QMetaObject::invokeMethod(m_typedPipelineWorker,
                                  &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::reset,
                                  Qt::QueuedConnection);
    }
    if (activeDeviceIsOpen()) {
        if (m_transportMode == TransportMode::TypedEvidence) {
            startTypedHandshakeWatchdog();
        } else {
            stopTypedHandshakeWatchdog();
        }
    }
}

void SerialWorker::ingestBytesForTest(const QByteArray& bytes) {
    processIncomingBytes(bytes);
}

void SerialWorker::processIncomingBytes(const QByteArray& bytes) {
    if (bytes.isEmpty()) return;
    CanMonitorPerf::ScopedProbe probe("serial.process_incoming_bytes", activeBytesToWrite(), 2000);
    if (m_transportMode == TransportMode::TypedEvidence) {
        processTypedBytes(bytes);
        return;
    }
    processLegacyBytes(bytes);
}

void SerialWorker::scheduleDrainPump() {
    ++m_drainEventTelemetry.scheduleDrainPumpCalls;
    if (m_drainPumpScheduled) {
        if (m_transportMode == TransportMode::TypedEvidence) ++m_drainEventTelemetry.typedWorkerInvokeSuppressed;
        return;
    }
    m_drainPumpScheduled = true;
    if (m_transportMode == TransportMode::TypedEvidence && m_typedPipelineWorker) {
        ++m_drainEventTelemetry.typedWorkerInvokeRequests;
        const qint64 handshakeElapsedMs = m_typedHandshakeClock.isValid() ? m_typedHandshakeClock.elapsed() : -1;
        QMetaObject::invokeMethod(m_typedPipelineWorker,
                                  [worker = m_typedPipelineWorker, handshakeElapsedMs]() {
                                      if (worker) worker->schedulePump(handshakeElapsedMs);
                                  },
                                  Qt::QueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(this, &SerialWorker::processDrainQueue, Qt::QueuedConnection);
}

void SerialWorker::processDrainQueue() {
    m_drainPumpScheduled = false;
    if (!m_drainQueue) return;
    const auto snapshotBefore = m_drainQueue->snapshot();
    QVector<CanMonitorTransport::DrainByteQueue::Block> blocks = m_drainQueue->popAll(kDrainProcessMaxBytes);
    if (blocks.isEmpty()) {
        emitDrainPipelineStatus();
        if (m_drainRuntime) {
            QMetaObject::invokeMethod(m_drainRuntime,
                                      &CanMonitorTransport::SerialDrainRuntime::acknowledgeBytesAvailable,
                                      Qt::QueuedConnection);
        }
        return;
    }
    QByteArray legacyBytes;
    if (m_transportMode != TransportMode::TypedEvidence) {
        qsizetype total = 0;
        for (const auto& block : blocks) total += block.bytes.size();
        legacyBytes.reserve(total);
        for (const auto& block : blocks) legacyBytes.append(block.bytes);
        processLegacyBytes(legacyBytes);
        emitDrainPipelineStatus();
        if (m_drainQueue->snapshot().usedBytes > 0) scheduleDrainPump();
        if (m_drainRuntime) {
            QMetaObject::invokeMethod(m_drainRuntime,
                                      &CanMonitorTransport::SerialDrainRuntime::acknowledgeBytesAvailable,
                                      Qt::QueuedConnection);
        }
        return;
    }

    CanMonitorPerf::ScopedProbe probe("serial.drain_pipeline_blocks_legacy_fallback", blocks.size(), 3000);
    const qint64 handshakeElapsedMs = m_typedHandshakeClock.isValid() ? m_typedHandshakeClock.elapsed() : -1;
    const auto result = m_typedPipeline.ingestBlocks(blocks,
                                                     handshakeElapsedMs,
                                                     snapshotBefore.usedBytes,
                                                     m_typedCaptureEnabled);
    if (result.capabilityFirstSeen) {
        qCInfo(logTransport).noquote()
            << "Typed CAPABILITY received after"
            << result.capabilityElapsedMs
            << "ms"
            << "bytes" << result.capabilityBytes;
        stopTypedHandshakeWatchdog();
    }
    for (const QString& error : result.errors) {
        emit errorOccurred(error);
    }
    for (const TypedRecordList& batch : result.recordBatches) handleTypedRecordBatch(batch);
    if (result.statusDue) emitTypedStatus(result.status);
    emitDrainPipelineStatus();
    if (m_drainQueue->snapshot().usedBytes > 0) {
        scheduleDrainPump();
    }
    if (m_drainRuntime) {
        QMetaObject::invokeMethod(m_drainRuntime,
                                  &CanMonitorTransport::SerialDrainRuntime::acknowledgeBytesAvailable,
                                  Qt::QueuedConnection);
    }
}

void SerialWorker::processLegacyBytes(const QByteArray& bytes) {
    CanMonitorPerf::ScopedProbe probe("serial.legacy_ingest", bytes.size(), 2000);
    const auto result = m_legacyIngress.ingest(bytes);
    for (const QString& error : result.errors) {
        emit errorOccurred(error);
    }
    for (const FrameRecordList& batch : result.frameBatches) {
        emit framesReceived(batch);
    }
    for (const StatsRecord& stats : result.stats) {
        emit statsReceived(stats);
    }
    if (result.loggingProgressDue) {
        emit loggingProgress(result.loggingBytesWritten, result.loggingFrameCount);
    }
}

void SerialWorker::processTypedBytes(const QByteArray& bytes) {
    CanMonitorPerf::ScopedProbe probe("serial.typed_ingest", bytes.size(), 2000);
    const qint64 handshakeElapsedMs = m_typedHandshakeClock.isValid() ? m_typedHandshakeClock.elapsed() : -1;
    CanMonitorTransport::DrainByteQueue::Block block;
    block.bytes = bytes;
    const auto result = m_typedPipeline.ingestBlocks(QVector<CanMonitorTransport::DrainByteQueue::Block>{block},
                                                     handshakeElapsedMs,
                                                     0,
                                                     m_typedCaptureEnabled);
    if (result.capabilityFirstSeen) {
        qCInfo(logTransport).noquote()
            << "Typed CAPABILITY received after"
            << result.capabilityElapsedMs
            << "ms"
            << "bytes" << result.capabilityBytes;
        stopTypedHandshakeWatchdog();
    }
    for (const QString& error : result.errors) {
        emit errorOccurred(error);
    }
    for (const TypedRecordList& batch : result.recordBatches) handleTypedRecordBatch(batch);
    if (result.statusDue) emitTypedStatus(result.status);
    emitDrainPipelineStatus();
}

void SerialWorker::setAnalysisConfig(const CanMonitorAnalysis::AnalysisRuntime::Config& config) {
    ensureAnalysisRuntime();
    QPointer<CanMonitorAnalysis::AnalysisWorkerRuntime> worker = m_analysisWorker;
    QMetaObject::invokeMethod(m_analysisWorker, [worker, config]() {
        if (worker) worker->setConfig(config);
    }, Qt::QueuedConnection);
}

void SerialWorker::emitTypedStatus(const CanMonitorTransport::TypedIngressRuntime::StatusSnapshot& status) {
    ++m_drainEventTelemetry.typedTransportStatusEmit;
    emit typedTransportStatusChanged(status.frames,
                                     status.bytesDropped,
                                     status.crcFailures,
                                     status.lengthFailures,
                                     status.versionWarnings,
                                     status.seqGaps);
}

void SerialWorker::handleTypedRecordBatch(const TypedRecordList& batch) {
    if (batch.isEmpty()) return;
    FrameRecordList canRxFrames;
    for (const TypedRecord& record : batch) {
        forEachCanRxFrame(record, [&](const FrameRecord& frame) {
            canRxFrames.push_back(frame);
        });
    }
    if (m_typedCaptureEnabled) queueCaptureWriterRecords(batch);
    queueAnalysisFrames(canRxFrames);
    queueRawLedgerFrames(canRxFrames);
    queueTruthFrames(batch);
    const auto projection = m_liveProjection.ingest(batch);
    if (!projection.criticalRecords.isEmpty()) emit typedRecordsReceived(projection.criticalRecords);
    if (!projection.projectedFrames.isEmpty()) queueProjectedFrames(projection.projectedFrames);
    if (projection.statusDue) emitProjectionStatus(projection.status);
}

quint64 SerialWorker::projectionKeyForFrame(const FrameRecord& frame) {
    quint64 key = (quint64(frame.bus) << 56);
    if (frame.ext) key |= (quint64(1) << 55);
    if (frame.rtr) key |= (quint64(1) << 54);
    key |= quint64(frame.canId & 0x1FFFFFFFU);
    return key;
}

void SerialWorker::queueProjectedFrames(const FrameRecordList& frames) {
    if (frames.isEmpty()) return;
    CanMonitorPerf::ScopedProbe probe("projection.queue_frames", m_pendingProjectionFramesByKey.size(), 1000);
    ++m_livePathTelemetry.projectedSignalReceived;
    m_livePathTelemetry.projectedFramesReceived += quint64(frames.size());

    for (const FrameRecord& frame : frames) {
        const quint64 key = projectionKeyForFrame(frame);
        auto existing = m_pendingProjectionFramesByKey.find(key);
        if (existing != m_pendingProjectionFramesByKey.end()) {
            existing.value() = frame;
            ++m_projectionQueueSampledFrames;
            continue;
        }
        if (m_pendingProjectionFramesByKey.size() >= kUiProjectionHardPendingKeys) {
            ++m_projectionQueueDroppedFrames;
            continue;
        }
        m_pendingProjectionFramesByKey.insert(key, frame);
    }
    m_livePathTelemetry.projectionQueueKeys = quint64(std::max<qsizetype>(0, m_pendingProjectionFramesByKey.size()));

    if (m_projectionFlushTimerId == 0) {
        m_projectionFlushTimerId = startTimer(kUiProjectionFlushIntervalMs, Qt::CoarseTimer);
    }
    if (!m_projectionFlushClock.isValid() ||
        m_projectionFlushClock.elapsed() >= kUiProjectionFlushIntervalMs) {
        flushQueuedProjectionFrames(false);
    }
}

void SerialWorker::flushQueuedProjectionFrames(bool force) {
    if (m_pendingProjectionFramesByKey.isEmpty()) return;
    CanMonitorPerf::ScopedProbe probe("projection.flush_frames", m_pendingProjectionFramesByKey.size(), 2000);
    if (!force &&
        m_projectionFlushClock.isValid() &&
        m_projectionFlushClock.elapsed() < kUiProjectionFlushIntervalMs) {
        return;
    }

    FrameRecordList frames;
    frames.reserve(m_pendingProjectionFramesByKey.size());
    for (auto it = m_pendingProjectionFramesByKey.cbegin(); it != m_pendingProjectionFramesByKey.cend(); ++it) {
        frames.push_back(it.value());
    }
    m_pendingProjectionFramesByKey.clear();
    m_livePathTelemetry.projectionQueueKeys = 0;
    ++m_livePathTelemetry.projectionFlushCount;

    std::sort(frames.begin(), frames.end(), [](const FrameRecord& a, const FrameRecord& b) {
        if (a.tExtUs != b.tExtUs) return a.tExtUs < b.tExtUs;
        if (a.bus != b.bus) return a.bus < b.bus;
        return a.canId < b.canId;
    });
    if (frames.size() > kUiProjectionMaxFramesPerFlush) {
        const int dropCount = frames.size() - kUiProjectionMaxFramesPerFlush;
        m_projectionQueueDroppedFrames += quint64(dropCount);
        frames.erase(frames.begin(), frames.begin() + dropCount);
    }

    if (!frames.isEmpty()) {
        ++m_livePathTelemetry.framesReceivedEmit;
        m_livePathTelemetry.framesReceivedFrames += quint64(frames.size());
        ++m_livePathTelemetry.framesReceivedEmitSeq;
        m_livePathTelemetry.framesReceivedLastEmitWallMs = QDateTime::currentMSecsSinceEpoch();
        emit framesReceived(frames);
        emitDrainPipelineStatus(true);
    }
    m_projectionFlushClock.restart();
    emitProjectionStatus(m_liveProjection.status());
}

void SerialWorker::queueRawLedgerFrames(const FrameRecordList& frames) {
    if (frames.isEmpty()) return;
    CanMonitorPerf::ScopedProbe probe("ledger.queue_frames", frames.size(), 2000);

    const quint64 incomingBytes = quint64(frames.size()) * quint64(kTypedCanRxSegmentHeaderSize + kTypedCanRxSegmentEntrySize + kTypedTransportFrameOverhead);
    if (m_pendingRawLedgerBytes + incomingBytes > kRawLedgerHandoffMaxBytes) {
        m_rawLedgerHandoffOverrunBytes += incomingBytes;
        m_drainEventTelemetry.rawLedgerHandoffOverrunBytes = m_rawLedgerHandoffOverrunBytes;
        emit rawLedgerWriterStatusChanged(m_pendingRawLedgerBytes,
                                          m_pendingRawLedgerMaxBytes,
                                          m_rawLedgerHandoffOverrunBytes,
                                          m_rawLedgerWriteMaxUs,
                                          m_rawLedgerWriteFailures,
                                          QStringLiteral("raw ledger handoff overrun"));
        return;
    }
    m_pendingRawLedgerFrames.reserve(m_pendingRawLedgerFrames.size() + frames.size());
    for (const FrameRecord& frame : frames) m_pendingRawLedgerFrames.push_back(frame);
    m_pendingRawLedgerBytes += incomingBytes;
    m_pendingRawLedgerMaxBytes = std::max(m_pendingRawLedgerMaxBytes, m_pendingRawLedgerBytes);
    m_drainEventTelemetry.rawLedgerHandoffPendingFrames = quint64(std::max<qsizetype>(0, m_pendingRawLedgerFrames.size()));
    m_drainEventTelemetry.rawLedgerHandoffPendingBytes = m_pendingRawLedgerBytes;
    m_drainEventTelemetry.rawLedgerHandoffMaxPendingBytes = m_pendingRawLedgerMaxBytes;
    if (m_rawLedgerFlushTimerId == 0) {
        m_rawLedgerFlushTimerId = startTimer(kRawLedgerFlushIntervalMs, Qt::CoarseTimer);
    }
    if (m_pendingRawLedgerFrames.size() >= kRawLedgerMaxRecordsPerFlush) {
        flushQueuedRawLedgerRecords(false);
    }
}

void SerialWorker::flushQueuedRawLedgerRecords(bool force) {
    Q_UNUSED(force);
    if (m_pendingRawLedgerFrames.isEmpty() || m_rawLedgerDispatchInFlight) return;
    ensureRawLedgerRuntime();
    CanMonitorPerf::ScopedProbe probe("ledger.flush_frames", m_pendingRawLedgerFrames.size(), 2000);
    FrameRecordList out;
    const int takeCount = std::min<int>(m_pendingRawLedgerFrames.size(), kRawLedgerMaxRecordsPerFlush);
    out.reserve(takeCount);
    const quint64 bytes = quint64(takeCount) * quint64(kTypedCanRxSegmentHeaderSize + kTypedCanRxSegmentEntrySize + kTypedTransportFrameOverhead);
    for (int index = 0; index < takeCount; ++index) out.push_back(m_pendingRawLedgerFrames.at(index));
    m_pendingRawLedgerFrames.erase(m_pendingRawLedgerFrames.begin(), m_pendingRawLedgerFrames.begin() + takeCount);
    m_pendingRawLedgerBytes = bytes > m_pendingRawLedgerBytes ? 0 : m_pendingRawLedgerBytes - bytes;
    m_rawLedgerDispatchInFlight = true;
    m_rawLedgerDispatchInFlightFrames = quint64(takeCount);
    ++m_drainEventTelemetry.rawLedgerHandoffDispatchCount;
    m_drainEventTelemetry.rawLedgerHandoffDispatchFrames += quint64(takeCount);
    m_drainEventTelemetry.rawLedgerHandoffPendingFrames = quint64(std::max<qsizetype>(0, m_pendingRawLedgerFrames.size()));
    m_drainEventTelemetry.rawLedgerHandoffPendingBytes = m_pendingRawLedgerBytes;
    m_drainEventTelemetry.rawLedgerHandoffMaxPendingBytes = m_pendingRawLedgerMaxBytes;
    m_drainEventTelemetry.rawLedgerHandoffInflight = true;
    QPointer<CanMonitorTransport::RawLedgerWriterRuntime> worker = m_rawLedgerWorker;
    QMetaObject::invokeMethod(m_rawLedgerWorker, [worker, frames = std::move(out)]() mutable {
        if (worker) worker->appendFrames(std::move(frames));
    }, Qt::QueuedConnection);
}

void SerialWorker::flushRawLedgerHandoffSync() {
    if (!m_rawLedgerWorker) return;
    if (!m_pendingRawLedgerFrames.isEmpty()) {
        FrameRecordList frames;
        frames.swap(m_pendingRawLedgerFrames);
        m_pendingRawLedgerBytes = 0;
        m_drainEventTelemetry.rawLedgerHandoffPendingFrames = 0;
        m_drainEventTelemetry.rawLedgerHandoffPendingBytes = 0;
        QPointer<CanMonitorTransport::RawLedgerWriterRuntime> worker = m_rawLedgerWorker;
        QMetaObject::invokeMethod(m_rawLedgerWorker, [worker, frames = std::move(frames)]() mutable {
            if (worker) worker->appendFrames(std::move(frames));
        }, Qt::BlockingQueuedConnection);
    } else if (m_rawLedgerDispatchInFlight) {
        QMetaObject::invokeMethod(m_rawLedgerWorker, []() {}, Qt::BlockingQueuedConnection);
    }
    m_rawLedgerDispatchInFlight = false;
    m_rawLedgerDispatchInFlightFrames = 0;
    m_drainEventTelemetry.rawLedgerHandoffInflight = false;
}

void SerialWorker::queueTruthFrames(const TypedRecordList& records) {
    CanMonitorPerf::ScopedProbe probe("truth.queue_records", records.size(), 2000);
    const auto result = m_liveTruth.ingest(records);
    if (!result.frames.isEmpty()) {
        ++m_drainEventTelemetry.truthHandoffEmitCount;
        m_drainEventTelemetry.truthHandoffEmitFrames += quint64(result.frames.size());
        emit truthFramesReceived(result.frames);
    }
    if (result.statusDue) emitTruthStatus(result.status);
    m_drainEventTelemetry.truthHandoffPendingKeys = quint64(std::max(0, m_liveTruth.status().pendingKeys));
    if (!m_liveTruth.hasPending()) return;
    if (m_truthFlushTimerId == 0) {
        m_truthFlushTimerId = startTimer(m_liveTruth.flushIntervalMs(), Qt::CoarseTimer);
    }
}

void SerialWorker::flushQueuedTruthFrames(bool force) {
    CanMonitorPerf::ScopedProbe probe("truth.flush_frames", m_liveTruth.status().pendingKeys, 2000);
    const FrameRecordList frames = m_liveTruth.flush(force);
    ++m_drainEventTelemetry.truthHandoffFlushCount;
    if (!frames.isEmpty()) {
        ++m_drainEventTelemetry.truthHandoffEmitCount;
        m_drainEventTelemetry.truthHandoffEmitFrames += quint64(frames.size());
        emit truthFramesReceived(frames);
    }
    m_drainEventTelemetry.truthHandoffPendingKeys = quint64(std::max(0, m_liveTruth.status().pendingKeys));
    emitTruthStatus(m_liveTruth.status());
}

void SerialWorker::queueAnalysisFrames(const FrameRecordList& frames) {
    CanMonitorPerf::ScopedProbe probe("analysis.ingest_frames", frames.size(), 3000);
    if (frames.isEmpty()) return;
    ensureAnalysisRuntime();
    const qsizetype available = std::max<qsizetype>(0, kAnalysisHandoffMaxFrames - m_pendingAnalysisFrames.size());
    const qsizetype accepted = std::min<qsizetype>(available, frames.size());
    if (accepted > 0) {
        m_pendingAnalysisFrames.reserve(m_pendingAnalysisFrames.size() + accepted);
        for (qsizetype index = 0; index < accepted; ++index) {
            m_pendingAnalysisFrames.push_back(frames.at(index));
        }
        m_drainEventTelemetry.analysisHandoffPendingFrames = quint64(std::max<qsizetype>(0, m_pendingAnalysisFrames.size()));
        m_drainEventTelemetry.analysisHandoffMaxPendingFrames =
            std::max(m_drainEventTelemetry.analysisHandoffMaxPendingFrames, m_drainEventTelemetry.analysisHandoffPendingFrames);
    }

    const qsizetype dropped = frames.size() - accepted;
    if (dropped > 0) {
        m_analysisHandoffOverrunFrames += quint64(dropped);
        m_drainEventTelemetry.analysisHandoffOverrunFrames = m_analysisHandoffOverrunFrames;
        QPointer<CanMonitorAnalysis::AnalysisWorkerRuntime> worker = m_analysisWorker;
        QMetaObject::invokeMethod(m_analysisWorker, [worker, dropped]() {
            if (worker) {
                worker->noteTruthLoss(quint64(dropped),
                                      QStringLiteral("Analysis handoff queue overrun: %1 CAN_RX frames").arg(dropped));
            }
        }, Qt::QueuedConnection);
    }

    scheduleAnalysisDispatch();
}

void SerialWorker::scheduleAnalysisDispatch() {
    if (m_analysisDispatchScheduled || m_analysisDispatchInFlight || m_pendingAnalysisFrames.isEmpty()) return;
    m_analysisDispatchScheduled = true;
    QMetaObject::invokeMethod(this, &SerialWorker::dispatchAnalysisFrames, Qt::QueuedConnection);
}

void SerialWorker::dispatchAnalysisFrames() {
    m_analysisDispatchScheduled = false;
    if (m_analysisDispatchInFlight || m_pendingAnalysisFrames.isEmpty()) return;
    ensureAnalysisRuntime();

    const int takeCount = std::min<int>(m_pendingAnalysisFrames.size(), kAnalysisHandoffDispatchFrames);
    FrameRecordList frames;
    frames.reserve(takeCount);
    for (int index = 0; index < takeCount; ++index) {
        frames.push_back(m_pendingAnalysisFrames.at(index));
    }
    m_pendingAnalysisFrames.erase(m_pendingAnalysisFrames.begin(), m_pendingAnalysisFrames.begin() + takeCount);

    m_analysisDispatchInFlight = true;
    m_analysisDispatchInFlightFrames = quint64(takeCount);
    ++m_drainEventTelemetry.analysisHandoffDispatchCount;
    m_drainEventTelemetry.analysisHandoffDispatchFrames += quint64(takeCount);
    m_drainEventTelemetry.analysisHandoffPendingFrames = quint64(std::max<qsizetype>(0, m_pendingAnalysisFrames.size()));
    m_drainEventTelemetry.analysisHandoffInflight = true;
    QPointer<CanMonitorAnalysis::AnalysisWorkerRuntime> worker = m_analysisWorker;
    QMetaObject::invokeMethod(m_analysisWorker, [worker, frames = std::move(frames)]() mutable {
        if (worker) worker->enqueueFrames(std::move(frames));
    }, Qt::QueuedConnection);
}

void SerialWorker::queueCaptureWriterRecords(const TypedRecordList& records) {
    if (records.isEmpty() || !m_captureWriterWorker) return;
    quint64 incomingBytes = 0;
    for (const TypedRecord& record : records) incomingBytes += quint64(record.frameBytes.size());
    if (incomingBytes + m_pendingCaptureWriterBytes > kCaptureWriterHandoffMaxBytes) {
        m_captureWriterHandoffOverrunBytes += incomingBytes;
        QPointer<CanMonitorTransport::TypedCaptureWriterWorkerRuntime> worker = m_captureWriterWorker;
        QMetaObject::invokeMethod(m_captureWriterWorker, [worker, count = quint64(records.size()), incomingBytes]() {
            if (worker) {
                worker->noteOverrun(count,
                                    incomingBytes,
                                    QStringLiteral("Typed capture writer handoff overrun: %1 bytes").arg(incomingBytes));
            }
        }, Qt::QueuedConnection);
        emitDrainPipelineStatus(true);
        return;
    }

    m_pendingCaptureWriterRecords.reserve(m_pendingCaptureWriterRecords.size() + records.size());
    for (const TypedRecord& record : records) m_pendingCaptureWriterRecords.push_back(record);
    m_pendingCaptureWriterBytes += incomingBytes;
    m_pendingCaptureWriterMaxBytes = std::max(m_pendingCaptureWriterMaxBytes, m_pendingCaptureWriterBytes);
    scheduleCaptureWriterDispatch();
}

void SerialWorker::scheduleCaptureWriterDispatch() {
    if (m_captureWriterDispatchScheduled ||
        m_captureWriterDispatchInFlight ||
        m_pendingCaptureWriterRecords.isEmpty()) {
        return;
    }
    m_captureWriterDispatchScheduled = true;
    QMetaObject::invokeMethod(this, &SerialWorker::dispatchCaptureWriterRecords, Qt::QueuedConnection);
}

void SerialWorker::dispatchCaptureWriterRecords() {
    m_captureWriterDispatchScheduled = false;
    if (m_captureWriterDispatchInFlight || m_pendingCaptureWriterRecords.isEmpty() || !m_captureWriterWorker) return;

    const int takeCount = std::min<int>(m_pendingCaptureWriterRecords.size(), kCaptureWriterHandoffDispatchRecords);
    TypedRecordList records;
    records.reserve(takeCount);
    quint64 bytes = 0;
    for (int index = 0; index < takeCount; ++index) {
        const TypedRecord& record = m_pendingCaptureWriterRecords.at(index);
        bytes += quint64(record.frameBytes.size());
        records.push_back(record);
    }
    m_pendingCaptureWriterRecords.erase(m_pendingCaptureWriterRecords.begin(),
                                        m_pendingCaptureWriterRecords.begin() + takeCount);
    m_pendingCaptureWriterBytes = bytes > m_pendingCaptureWriterBytes ? 0 : m_pendingCaptureWriterBytes - bytes;

    m_captureWriterDispatchInFlight = true;
    QPointer<CanMonitorTransport::TypedCaptureWriterWorkerRuntime> worker = m_captureWriterWorker;
    QMetaObject::invokeMethod(m_captureWriterWorker, [worker, records = std::move(records)]() mutable {
        if (worker) worker->enqueueRecords(std::move(records));
    }, Qt::QueuedConnection);
}

void SerialWorker::flushCaptureWriterHandoffSync() {
    if (!m_captureWriterWorker) return;

    while (m_captureRecordQueue && m_captureRecordQueue->hasQueuedRecords()) {
        QMetaObject::invokeMethod(m_captureWriterWorker,
                                  &CanMonitorTransport::TypedCaptureWriterWorkerRuntime::drainQueuedRecords,
                                  Qt::BlockingQueuedConnection);
    }

    if (!m_pendingCaptureWriterRecords.isEmpty()) {
        TypedRecordList records;
        records.swap(m_pendingCaptureWriterRecords);
        m_pendingCaptureWriterBytes = 0;
        QPointer<CanMonitorTransport::TypedCaptureWriterWorkerRuntime> worker = m_captureWriterWorker;
        QMetaObject::invokeMethod(m_captureWriterWorker, [worker, records = std::move(records)]() mutable {
            if (worker) worker->enqueueRecords(std::move(records));
        }, Qt::BlockingQueuedConnection);
    }

    m_captureWriterDispatchScheduled = false;
    m_captureWriterDispatchInFlight = false;
}

void SerialWorker::emitProjectionStatus(const CanMonitorTransport::LiveProjectionRuntime::Status& status) {
    m_lastProjectionStatus = status;
    ++m_drainEventTelemetry.typedProjectionStatusEmit;
    emit typedProjectionStatusChanged(status.observedCanRxFrames,
                                      status.projectedCanRxFrames,
                                      status.sampledCanRxFrames + m_projectionQueueSampledFrames,
                                      status.workerDroppedCanRxFrames + m_projectionQueueDroppedFrames,
                                      status.observedBus0CanRxFrames,
                                      status.observedBus1CanRxFrames,
                                      status.observedControlEvidenceRecords,
                                      status.projectedControlEvidenceRecords,
                                      status.sampledControlEvidenceRecords);
}

void SerialWorker::emitTruthStatus(const CanMonitorTransport::LiveTruthRuntime::Status& status) {
    ++m_drainEventTelemetry.typedTruthStatusEmit;
    emit typedTruthStatusChanged(status.observedCanRxFrames,
                                 status.emittedTruthFrames,
                                 status.coalescedTruthUpdates,
                                 status.observedBus0CanRxFrames,
                                 status.observedBus1CanRxFrames,
                                 status.flushCount,
                                 status.pendingKeys,
                                 status.maxPendingKeys,
                                 status.lastInputRecords,
                                 status.lastOutputFrames,
                                 status.lastFlushMs,
                                 status.truthLoss);
}

void SerialWorker::resetProjectionQueue() {
    if (m_projectionFlushTimerId != 0) {
        killTimer(m_projectionFlushTimerId);
        m_projectionFlushTimerId = 0;
    }
    if (m_truthFlushTimerId != 0) {
        killTimer(m_truthFlushTimerId);
        m_truthFlushTimerId = 0;
    }
    if (m_rawLedgerFlushTimerId != 0) {
        killTimer(m_rawLedgerFlushTimerId);
        m_rawLedgerFlushTimerId = 0;
    }
    m_pendingProjectionFramesByKey.clear();
    m_pendingRawLedgerFrames.clear();
    m_pendingAnalysisFrames.clear();
    m_analysisDispatchScheduled = false;
    m_analysisDispatchInFlight = false;
    m_analysisDispatchInFlightFrames = 0;
    m_rawLedgerDispatchInFlight = false;
    m_rawLedgerDispatchInFlightFrames = 0;
    m_projectionFlushClock.invalidate();
    m_liveTruth.reset();
    resetAnalysisWorker();
    m_projectionQueueSampledFrames = 0;
    m_projectionQueueDroppedFrames = 0;
    m_lastProjectionStatus = {};
    m_livePathTelemetry = CanMonitorTransport::LivePathTelemetry{};
}

void SerialWorker::emitLegacyLoggingUpdate(const CanMonitorTransport::LegacyIngressRuntime::LoggingUpdate& update) {
    if (!update.error.isEmpty()) {
        emit errorOccurred(update.error);
    }
    if (update.progressDue) {
        emit loggingProgress(update.bytesWritten, update.frameCount);
    }
    if (update.stateChanged) {
        emit loggingStateChanged(update.active, update.path);
    }
}

void SerialWorker::emitTypedStorageUpdate(const CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate& update) {
    if (!update.ok) {
        emit errorOccurred(update.error);
    }
    if (update.progressDue) {
        emit typedStorageProgress(update.bytesWritten, update.recordCount);
    }
    if (update.stateChanged) {
        emit typedStorageStateChanged(update.active, update.path);
    }
}

void SerialWorker::startTypedHandshakeWatchdog() {
    stopTypedHandshakeWatchdog();
    m_typedPipeline.reset();
    m_pipelineStatus = {};
    m_pipelineCaptureDiagnostics = {};
    if (m_typedPipelineWorker) {
        QMetaObject::invokeMethod(m_typedPipelineWorker,
                                  &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::reset,
                                  Qt::QueuedConnection);
    }
    m_liveProjection.reset();
    resetProjectionQueue();
    m_typedHandshakeClock.restart();
    m_typedHandshakeTimerId = startTimer(kTypedHandshakeWatchdogIntervalMs, Qt::CoarseTimer);
}

void SerialWorker::stopTypedHandshakeWatchdog() {
    if (m_typedHandshakeTimerId != 0) {
        killTimer(m_typedHandshakeTimerId);
        m_typedHandshakeTimerId = 0;
    }
    m_typedHandshakeClock.invalidate();
}

void SerialWorker::closeSerialPortForRecovery(const QString& reason) {
    stopTypedHandshakeWatchdog();
    stopControlCycle();
    flushQueuedProjectionFrames(true);
    flushQueuedTruthFrames(true);
    emitTypedStorageUpdate(finalizeCaptureWriterIfActive());
    if (!m_connected && !m_drainRuntime && !m_serial && !m_tcp) return;

    const QString transportName = activeTransportName();
    qCInfo(logTransport).noquote() << "Closing transport" << transportName << reason;
    if (m_serial) {
        disconnect(m_serial, nullptr, this, nullptr);
        if (m_serial->isOpen()) {
            m_serial->clear(QSerialPort::AllDirections);
            m_serial->setRequestToSend(false);
            m_serial->setDataTerminalReady(false);
            m_serial->close();
        }
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    if (m_tcp) {
        disconnect(m_tcp, nullptr, this, nullptr);
        if (m_tcp->isOpen()) {
            m_tcp->disconnectFromHost();
            if (m_tcp->state() != QAbstractSocket::UnconnectedState) {
                m_tcp->waitForDisconnected(100);
            }
            m_tcp->close();
        }
        m_tcp->deleteLater();
        m_tcp = nullptr;
    }
    if (m_drainRuntime) {
        QMetaObject::invokeMethod(m_drainRuntime, &CanMonitorTransport::SerialDrainRuntime::stop, Qt::QueuedConnection);
    }
    m_connected = false;
    m_typedPipeline.reset();
    m_liveProjection.reset();
    resetProjectionQueue();
}

bool SerialWorker::startGatewayTcp(const QString& endpoint) {
    ensureDrainRuntime();
    if (m_transportMode == TransportMode::TypedEvidence) {
        startTypedHandshakeWatchdog();
    }
    QMetaObject::invokeMethod(m_drainRuntime, [runtime = m_drainRuntime, endpoint]() {
        runtime->startGatewayTcp(endpoint);
    }, Qt::QueuedConnection);
    return true;
}

void SerialWorker::ensureDrainRuntime() {
    if (m_drainRuntime) return;
    if (!m_drainQueue) m_drainQueue.reset(new CanMonitorTransport::DrainByteQueue());
    m_drainRuntime = new CanMonitorTransport::SerialDrainRuntime(m_drainQueue);
    m_drainRuntime->moveToThread(&m_drainThread);
    connect(&m_drainThread, &QThread::finished, m_drainRuntime, &QObject::deleteLater);
    connect(m_drainRuntime, &CanMonitorTransport::SerialDrainRuntime::stateChanged, this, [this](bool connected, const QString& message) {
        m_connected = connected;
        emit stateChanged(connected, message);
    }, Qt::QueuedConnection);
    connect(m_drainRuntime, &CanMonitorTransport::SerialDrainRuntime::errorOccurred, this, &SerialWorker::errorOccurred, Qt::QueuedConnection);
    connect(m_drainRuntime, &CanMonitorTransport::SerialDrainRuntime::bytesAvailable, this, &SerialWorker::scheduleDrainPump, Qt::QueuedConnection);
    connect(m_drainRuntime, &CanMonitorTransport::SerialDrainRuntime::hostFrameWriteResult, this, &SerialWorker::hostFrameWriteResult, Qt::QueuedConnection);
    connect(m_drainRuntime, &CanMonitorTransport::SerialDrainRuntime::hostTxQueueChanged, this, &SerialWorker::hostTxQueueChanged, Qt::QueuedConnection);
    connect(m_drainRuntime,
            &CanMonitorTransport::SerialDrainRuntime::drainEventTraceChanged,
            this,
            [this](const QJsonObject& trace) {
                m_drainRuntimeEventTrace = trace;
            },
            Qt::QueuedConnection);
    connect(m_drainRuntime,
            &CanMonitorTransport::SerialDrainRuntime::drainStatusChanged,
            this,
            [this](quint64 bytesTotal,
                   quint64 readyReadCount,
                   quint64 readyReadMaxUs,
                   quint64 drainBurstMaxBytes,
                   quint64 rawQueueUsedBytes,
                   quint64 rawQueueMaxUsedBytes,
                   quint64 rawQueueCapacityBytes,
                   quint64 rawQueueOverrunBytes,
                   quint64 rawQueueContentionCount) {
                ++m_drainEventTelemetry.drainStatusReceived;
                m_drainBytesTotal = bytesTotal;
                m_drainReadyReadCount = readyReadCount;
                m_drainReadyReadMaxUs = readyReadMaxUs;
                m_drainBurstMaxBytes = drainBurstMaxBytes;
                m_drainRawQueueUsedBytes = rawQueueUsedBytes;
                m_drainRawQueueMaxUsedBytes = rawQueueMaxUsedBytes;
                m_drainRawQueueCapacityBytes = rawQueueCapacityBytes;
                m_drainRawQueueOverrunBytes = rawQueueOverrunBytes;
                m_drainRawQueueContentionCount = rawQueueContentionCount;
                emitDrainPipelineStatus();
            },
            Qt::QueuedConnection);
    m_drainThread.start(QThread::TimeCriticalPriority);
}

void SerialWorker::shutdownDrainRuntime() {
    if (m_drainRuntime) {
        QMetaObject::invokeMethod(m_drainRuntime, &CanMonitorTransport::SerialDrainRuntime::stop, Qt::BlockingQueuedConnection);
    }
    if (m_drainThread.isRunning()) {
        m_drainThread.quit();
        m_drainThread.wait();
    }
    m_drainRuntime = nullptr;
    m_connected = false;
    m_drainPumpScheduled = false;
    m_drainEventTelemetry = CanMonitorTransport::DrainEventTelemetry{};
    m_drainRuntimeEventTrace = {};
    m_pipelineLivePathTrace = {};
    m_pipelineDrainEventTrace = {};
}

void SerialWorker::ensureTypedPipelineRuntime() {
    if (m_typedPipelineWorker) return;
    if (!m_drainQueue) m_drainQueue.reset(new CanMonitorTransport::DrainByteQueue());
    if (!m_captureRecordQueue) m_captureRecordQueue.reset(new CanMonitorTransport::TypedRecordHandoffQueue());
    m_typedPipelineWorker = new CanMonitorTransport::TypedEvidencePipelineWorkerRuntime(m_drainQueue, m_captureRecordQueue);
    m_typedPipelineWorker->moveToThread(&m_typedPipelineThread);
    connect(&m_typedPipelineThread, &QThread::finished, m_typedPipelineWorker, &QObject::deleteLater);
    const bool captureEnabled = m_typedCaptureEnabled;
    QMetaObject::invokeMethod(m_typedPipelineWorker,
                              [worker = QPointer<CanMonitorTransport::TypedEvidencePipelineWorkerRuntime>(m_typedPipelineWorker), captureEnabled]() {
                                  if (worker) {
                                      worker->setCaptureEnabled(captureEnabled);
                                      worker->setSecondaryFanoutEnabled(false);
                                  }
                              },
                              Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::capabilityFirstSeen,
            this,
            [this](qint64 elapsedMs, quint64 bytes) {
                qCInfo(logTransport).noquote()
                    << "Typed CAPABILITY received after"
                    << elapsedMs
                    << "ms"
                    << "bytes" << bytes;
                stopTypedHandshakeWatchdog();
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::errorsOccurred,
            this,
            [this](const QStringList& errors) {
                for (const QString& error : errors) emit errorOccurred(error);
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::canRxFramesReady,
            this,
            [this](const FrameRecordList& frames) {
                queueAnalysisFrames(frames);
                queueRawLedgerFrames(frames);
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::projectedFramesReady,
            this,
            [this](const FrameRecordList& frames) {
                queueProjectedFrames(frames);
                if (m_typedPipelineWorker) {
                    QMetaObject::invokeMethod(m_typedPipelineWorker,
                                              &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::acknowledgeProjectionSnapshot,
                                              Qt::QueuedConnection);
                }
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::truthFramesReady,
            this,
            &SerialWorker::truthFramesReceived,
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::criticalRecordsReady,
            this,
            &SerialWorker::typedRecordsReceived,
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::captureQueueReady,
            this,
            [this]() {
                if (!m_captureWriterWorker) return;
                QMetaObject::invokeMethod(m_captureWriterWorker,
                                          &CanMonitorTransport::TypedCaptureWriterWorkerRuntime::drainQueuedRecords,
                                          Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::captureHandoffOverrun,
            this,
            [this](quint64 records, quint64 bytes, const QString& reason) {
                m_captureWriterHandoffOverrunBytes += bytes;
                if (m_captureWriterWorker) {
                    QPointer<CanMonitorTransport::TypedCaptureWriterWorkerRuntime> worker = m_captureWriterWorker;
                    QMetaObject::invokeMethod(m_captureWriterWorker, [worker, records, bytes, reason]() {
                        if (worker) worker->noteOverrun(records, bytes, reason);
                    }, Qt::QueuedConnection);
                } else {
                    emit errorOccurred(reason);
                }
                emitDrainPipelineStatus(true);
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::projectionStatusReady,
            this,
            [this](quint64 observedCanRxFrames,
                   quint64 projectedCanRxFrames,
                   quint64 sampledCanRxFrames,
                   quint64 workerDroppedCanRxFrames,
                   quint64 observedBus0CanRxFrames,
                   quint64 observedBus1CanRxFrames,
                   quint64 observedControlEvidenceRecords,
                   quint64 projectedControlEvidenceRecords,
                   quint64 sampledControlEvidenceRecords) {
                ++m_drainEventTelemetry.typedProjectionStatusReceive;
                ++m_drainEventTelemetry.typedProjectionStatusEmit;
                emit typedProjectionStatusChanged(observedCanRxFrames,
                                                  projectedCanRxFrames,
                                                  sampledCanRxFrames + m_projectionQueueSampledFrames,
                                                  workerDroppedCanRxFrames + m_projectionQueueDroppedFrames,
                                                  observedBus0CanRxFrames,
                                                  observedBus1CanRxFrames,
                                                  observedControlEvidenceRecords,
                                                  projectedControlEvidenceRecords,
                                                  sampledControlEvidenceRecords);
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::truthStatusReady,
            this,
            [this](quint64 observedCanRxFrames,
                   quint64 emittedTruthFrames,
                   quint64 coalescedTruthUpdates,
                   quint64 observedBus0CanRxFrames,
                   quint64 observedBus1CanRxFrames,
                   quint64 flushCount,
                   int pendingKeys,
                   int maxPendingKeys,
                   int lastInputRecords,
                   int lastOutputFrames,
                   int lastFlushMs,
                   quint64 truthLoss) {
                ++m_drainEventTelemetry.typedTruthStatusReceive;
                ++m_drainEventTelemetry.typedTruthStatusEmit;
                emit typedTruthStatusChanged(observedCanRxFrames,
                                             emittedTruthFrames,
                                             coalescedTruthUpdates,
                                             observedBus0CanRxFrames,
                                             observedBus1CanRxFrames,
                                             flushCount,
                                             pendingKeys,
                                             maxPendingKeys,
                                             lastInputRecords,
                                             lastOutputFrames,
                                             lastFlushMs,
                                             truthLoss);
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::typedStatusReady,
            this,
            [this](quint64 frames,
                   quint64 bytesDropped,
                   quint64 crcFailures,
                   quint64 lengthFailures,
                   quint64 versionWarnings,
                   quint64 seqGaps) {
                ++m_drainEventTelemetry.typedTransportStatusReceive;
                ++m_drainEventTelemetry.typedTransportStatusEmit;
                emit typedTransportStatusChanged(frames,
                                                 bytesDropped,
                                                 crcFailures,
                                                 lengthFailures,
                                                 versionWarnings,
                                                 seqGaps);
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::pipelineStatusChanged,
            this,
            [this](quint64 parseBacklogBytes, quint64 parserBatchMaxMs) {
                m_pipelineStatus.parseBacklogBytes = parseBacklogBytes;
                m_pipelineStatus.parserBatchMaxMs = parserBatchMaxMs;
                emitDrainPipelineStatus();
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::captureDiagnosticsChanged,
            this,
            [this](const QJsonObject& diagnostics) {
                m_pipelineCaptureDiagnostics = diagnostics;
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::pipelineTraceChanged,
            this,
            [this](const QJsonObject& livePathTrace, const QJsonObject& drainEventTrace) {
                m_pipelineLivePathTrace = livePathTrace;
                m_pipelineDrainEventTrace = drainEventTrace;
            },
            Qt::QueuedConnection);
    connect(m_typedPipelineWorker,
            &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::pumpCycleFinished,
            this,
            [this]() {
                m_drainPumpScheduled = false;
                if (m_drainRuntime) {
                    QMetaObject::invokeMethod(m_drainRuntime,
                                              &CanMonitorTransport::SerialDrainRuntime::acknowledgeBytesAvailable,
                                              Qt::QueuedConnection);
                }
            },
            Qt::QueuedConnection);
    m_typedPipelineThread.start(QThread::HighPriority);
}

void SerialWorker::shutdownTypedPipelineRuntime() {
    if (m_typedPipelineWorker) {
        QMetaObject::invokeMethod(m_typedPipelineWorker,
                                  &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::reset,
                                  Qt::BlockingQueuedConnection);
    }
    if (m_typedPipelineThread.isRunning()) {
        m_typedPipelineThread.quit();
        m_typedPipelineThread.wait();
    }
    m_typedPipelineWorker = nullptr;
    m_pipelineStatus = {};
    m_pipelineCaptureDiagnostics = {};
}

void SerialWorker::ensureAnalysisRuntime() {
    if (m_analysisWorker) return;
    m_analysisWorker = new CanMonitorAnalysis::AnalysisWorkerRuntime();
    m_analysisWorker->moveToThread(&m_analysisThread);
    connect(&m_analysisThread, &QThread::finished, m_analysisWorker, &QObject::deleteLater);
    connect(m_analysisWorker,
            &CanMonitorAnalysis::AnalysisWorkerRuntime::snapshotReady,
            this,
            &SerialWorker::analysisRuntimeSnapshotChanged,
            Qt::QueuedConnection);
    connect(m_analysisWorker,
            &CanMonitorAnalysis::AnalysisWorkerRuntime::statusChanged,
            this,
            &SerialWorker::analysisQueueStatusChanged,
            Qt::QueuedConnection);
    connect(m_analysisWorker,
            &CanMonitorAnalysis::AnalysisWorkerRuntime::errorOccurred,
            this,
            &SerialWorker::errorOccurred,
            Qt::QueuedConnection);
    connect(m_analysisWorker,
            &CanMonitorAnalysis::AnalysisWorkerRuntime::handoffDrained,
            this,
            [this]() {
                m_analysisDispatchInFlight = false;
                ++m_drainEventTelemetry.analysisHandoffCompleteCount;
                m_drainEventTelemetry.analysisHandoffCompleteFrames += m_analysisDispatchInFlightFrames;
                m_analysisDispatchInFlightFrames = 0;
                m_drainEventTelemetry.analysisHandoffInflight = false;
                m_drainEventTelemetry.analysisHandoffPendingFrames =
                    quint64(std::max<qsizetype>(0, m_pendingAnalysisFrames.size()));
                scheduleAnalysisDispatch();
            },
            Qt::QueuedConnection);
    m_analysisThread.start(QThread::HighPriority);
}

void SerialWorker::shutdownAnalysisRuntime() {
    m_pendingAnalysisFrames.clear();
    m_analysisDispatchScheduled = false;
    m_analysisDispatchInFlight = false;
    if (m_analysisWorker) {
        QMetaObject::invokeMethod(m_analysisWorker, &CanMonitorAnalysis::AnalysisWorkerRuntime::reset, Qt::BlockingQueuedConnection);
    }
    if (m_analysisThread.isRunning()) {
        m_analysisThread.quit();
        m_analysisThread.wait();
    }
    m_analysisWorker = nullptr;
}

void SerialWorker::ensureCaptureWriterRuntime() {
    if (m_captureWriterWorker) return;
    if (!m_captureRecordQueue) m_captureRecordQueue.reset(new CanMonitorTransport::TypedRecordHandoffQueue());
    m_captureWriterWorker = new CanMonitorTransport::TypedCaptureWriterWorkerRuntime();
    m_captureWriterWorker->setRecordQueue(m_captureRecordQueue);
    m_captureWriterWorker->moveToThread(&m_captureWriterThread);
    connect(&m_captureWriterThread, &QThread::finished, m_captureWriterWorker, &QObject::deleteLater);
    connect(m_captureWriterWorker,
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
                emitTypedStorageUpdate(update);
            },
            Qt::QueuedConnection);
    connect(m_captureWriterWorker,
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
                Q_UNUSED(active);
                Q_UNUSED(captureInvalid);
                Q_UNUSED(queuedRecords);
                Q_UNUSED(overrunRecords);
                m_captureWriterQueueBytes = queuedBytes;
                m_captureWriterMaxQueueBytes = maxQueuedBytes;
                m_captureWriterOverrunBytes = overrunBytes;
                m_captureWriteMaxMs = writeMaxMs;
                emitDrainPipelineStatus(overrunBytes > 0);
            },
            Qt::QueuedConnection);
    connect(m_captureWriterWorker,
            &CanMonitorTransport::TypedCaptureWriterWorkerRuntime::batchFinished,
            this,
            [this]() {
                m_captureWriterDispatchInFlight = false;
                scheduleCaptureWriterDispatch();
            },
            Qt::QueuedConnection);
    m_captureWriterThread.start(QThread::HighPriority);
}

void SerialWorker::shutdownCaptureWriterRuntime() {
    finalizeCaptureWriterIfActive();
    if (m_captureWriterWorker) {
        QMetaObject::invokeMethod(m_captureWriterWorker, &CanMonitorTransport::TypedCaptureWriterWorkerRuntime::resetQueue, Qt::BlockingQueuedConnection);
    }
    if (m_captureWriterThread.isRunning()) {
        m_captureWriterThread.quit();
        m_captureWriterThread.wait();
    }
    m_captureWriterWorker = nullptr;
    m_captureWriterQueueBytes = 0;
    m_captureWriterMaxQueueBytes = 0;
    m_captureWriterOverrunBytes = 0;
    m_captureWriteMaxMs = 0;
    m_pendingCaptureWriterRecords.clear();
    if (m_captureRecordQueue) m_captureRecordQueue->clear();
    m_pendingCaptureWriterBytes = 0;
    m_pendingCaptureWriterMaxBytes = 0;
    m_captureWriterHandoffOverrunBytes = 0;
    m_captureWriterDispatchScheduled = false;
    m_captureWriterDispatchInFlight = false;
}

void SerialWorker::ensureRawLedgerRuntime() {
    if (m_rawLedgerWorker) return;
    m_rawLedgerWorker = new CanMonitorTransport::RawLedgerWriterRuntime();
    m_rawLedgerWorker->moveToThread(&m_rawLedgerThread);
    connect(&m_rawLedgerThread, &QThread::finished, m_rawLedgerWorker, &QObject::deleteLater);
    connect(m_rawLedgerWorker,
            &CanMonitorTransport::RawLedgerWriterRuntime::resetCompleted,
            this,
            &SerialWorker::rawLedgerReset,
            Qt::QueuedConnection);
    connect(m_rawLedgerWorker,
            &CanMonitorTransport::RawLedgerWriterRuntime::batchCommitted,
            this,
            &SerialWorker::rawLedgerBatchCommitted,
            Qt::QueuedConnection);
    connect(m_rawLedgerWorker,
            &CanMonitorTransport::RawLedgerWriterRuntime::statusChanged,
            this,
            [this](quint64 totalRows,
                   quint64 segmentBytes,
                   quint64 batchCount,
                   quint64 writeMaxUs,
                   quint64 writeFailures,
                   const QString& lastError) {
                Q_UNUSED(totalRows);
                Q_UNUSED(segmentBytes);
                Q_UNUSED(batchCount);
                m_rawLedgerWriteMaxUs = std::max(m_rawLedgerWriteMaxUs, writeMaxUs);
                m_rawLedgerWriteFailures = writeFailures;
                m_rawLedgerLastError = lastError;
                emit rawLedgerWriterStatusChanged(m_pendingRawLedgerBytes,
                                                  m_pendingRawLedgerMaxBytes,
                                                  m_rawLedgerHandoffOverrunBytes,
                                                  m_rawLedgerWriteMaxUs,
                                                  m_rawLedgerWriteFailures,
                                                  m_rawLedgerLastError);
            },
            Qt::QueuedConnection);
    connect(m_rawLedgerWorker,
            &CanMonitorTransport::RawLedgerWriterRuntime::batchFinished,
            this,
            [this]() {
                m_rawLedgerDispatchInFlight = false;
                ++m_drainEventTelemetry.rawLedgerHandoffCompleteCount;
                m_drainEventTelemetry.rawLedgerHandoffCompleteFrames += m_rawLedgerDispatchInFlightFrames;
                m_rawLedgerDispatchInFlightFrames = 0;
                m_drainEventTelemetry.rawLedgerHandoffInflight = false;
                m_drainEventTelemetry.rawLedgerHandoffPendingFrames =
                    quint64(std::max<qsizetype>(0, m_pendingRawLedgerFrames.size()));
                m_drainEventTelemetry.rawLedgerHandoffPendingBytes = m_pendingRawLedgerBytes;
                if (!m_pendingRawLedgerFrames.isEmpty()) flushQueuedRawLedgerRecords(false);
            },
            Qt::QueuedConnection);
    m_rawLedgerThread.start(QThread::HighPriority);
}

void SerialWorker::shutdownRawLedgerRuntime() {
    flushRawLedgerHandoffSync();
    if (m_rawLedgerThread.isRunning()) {
        m_rawLedgerThread.quit();
        m_rawLedgerThread.wait();
    }
    m_rawLedgerWorker = nullptr;
    m_pendingRawLedgerFrames.clear();
    m_pendingRawLedgerBytes = 0;
    m_pendingRawLedgerMaxBytes = 0;
    m_rawLedgerHandoffOverrunBytes = 0;
    m_rawLedgerWriteMaxUs = 0;
    m_rawLedgerWriteFailures = 0;
    m_rawLedgerLastError.clear();
    m_rawLedgerDispatchInFlight = false;
    m_rawLedgerDispatchInFlightFrames = 0;
}

CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate SerialWorker::finalizeCaptureWriterIfActive() {
    CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate update;
    m_typedCaptureEnabled = false;
    if (m_typedPipelineWorker) {
        QMetaObject::invokeMethod(m_typedPipelineWorker,
                                  [worker = QPointer<CanMonitorTransport::TypedEvidencePipelineWorkerRuntime>(m_typedPipelineWorker)]() {
                                      if (worker) worker->setCaptureEnabled(false);
                                  },
                                  Qt::BlockingQueuedConnection);
    }
    if (!m_captureWriterWorker) return update;
    flushCaptureWriterHandoffSync();
    QJsonObject diagnostics = m_pipelineCaptureDiagnostics.isEmpty()
        ? m_typedPipeline.makeCaptureDiagnostics()
        : m_pipelineCaptureDiagnostics;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QJsonObject liveTrace = m_pipelineLivePathTrace;
    mergeTrace(liveTrace, serialLiveTraceJson(m_livePathTelemetry, nowMs));
    QJsonObject drainTrace = m_drainRuntimeEventTrace;
    mergeTrace(drainTrace, m_pipelineDrainEventTrace);
    mergeTrace(drainTrace, serialDrainTraceJson(m_drainEventTelemetry, nowMs));
    diagnostics.insert(QStringLiteral("live_path_trace"), liveTrace);
    diagnostics.insert(QStringLiteral("drain_event_trace"), drainTrace);
    QMetaObject::invokeMethod(m_captureWriterWorker, [this, diagnostics, &update]() {
        update = m_captureWriterWorker->finalizeStorageIfActiveSync(diagnostics);
    }, Qt::BlockingQueuedConnection);
    return update;
}

void SerialWorker::resetAnalysisWorker() {
    if (!m_analysisWorker) return;
    QPointer<CanMonitorAnalysis::AnalysisWorkerRuntime> worker = m_analysisWorker;
    QMetaObject::invokeMethod(m_analysisWorker, [worker]() {
        if (worker) worker->reset();
    }, Qt::QueuedConnection);
}

void SerialWorker::emitDrainPipelineStatus(bool force) {
    if (!force &&
        m_drainStatusClock.isValid() &&
        m_drainStatusClock.elapsed() < kDrainPipelineStatusIntervalMs) {
        return;
    }
    m_drainStatusClock.restart();
    const auto pipeline = (m_pipelineStatus.parserBatchMaxMs > 0 || m_pipelineStatus.parseBacklogBytes > 0)
        ? m_pipelineStatus
        : m_typedPipeline.status();
    const auto captureHandoff = m_captureRecordQueue
        ? m_captureRecordQueue->snapshot()
        : CanMonitorTransport::TypedRecordHandoffQueue::Snapshot{};
    const quint64 captureWriterQueueBytes = m_captureWriterQueueBytes
        + m_pendingCaptureWriterBytes
        + captureHandoff.queuedBytes;
    const quint64 captureWriterMaxQueueBytes = std::max({m_captureWriterMaxQueueBytes,
                                                         m_pendingCaptureWriterMaxBytes,
                                                         captureHandoff.maxQueuedBytes});
    const quint64 captureWriterOverrunBytes = std::max({m_captureWriterOverrunBytes,
                                                        m_captureWriterHandoffOverrunBytes,
                                                        captureHandoff.overrunBytes});
    m_drainEventTelemetry.drainPumpScheduledFlag = m_drainPumpScheduled;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QJsonObject liveTrace = m_pipelineLivePathTrace;
    mergeTrace(liveTrace, serialLiveTraceJson(m_livePathTelemetry, nowMs));
    QJsonObject drainTrace = m_drainRuntimeEventTrace;
    mergeTrace(drainTrace, m_pipelineDrainEventTrace);
    mergeTrace(drainTrace, serialDrainTraceJson(m_drainEventTelemetry, nowMs));
    emit drainStatusChanged(m_drainBytesTotal,
                            m_drainReadyReadCount,
                            m_drainReadyReadMaxUs,
                            m_drainBurstMaxBytes,
                            m_drainRawQueueUsedBytes,
                            m_drainRawQueueMaxUsedBytes,
                            m_drainRawQueueCapacityBytes,
                            m_drainRawQueueOverrunBytes,
                            m_drainRawQueueContentionCount,
                            pipeline.parseBacklogBytes,
                            pipeline.parserBatchMaxMs,
                            captureWriterQueueBytes,
                            captureWriterMaxQueueBytes,
                            captureWriterOverrunBytes,
                            m_captureWriteMaxMs);
    emit livePathTraceChanged(liveTrace);
    emit drainEventTraceChanged(drainTrace);
}

QIODevice* SerialWorker::activeDevice() const {
    if (m_serial) return m_serial;
    if (m_tcp) return m_tcp;
    return nullptr;
}

bool SerialWorker::activeDeviceIsOpen() const {
    const QIODevice* device = activeDevice();
    return device && device->isOpen();
}

bool SerialWorker::activeDeviceIsWritable() const {
    const QIODevice* device = activeDevice();
    return device && device->isOpen() && device->isWritable();
}

qint64 SerialWorker::activeBytesToWrite() const {
    if (m_serial) return m_serial->bytesToWrite();
    if (m_tcp) return m_tcp->bytesToWrite();
    return 0;
}

QString SerialWorker::activeTransportName() const {
    if (m_serial) return m_serial->portName();
    if (m_tcp) return QStringLiteral("tcp://%1:%2").arg(m_tcp->peerName()).arg(m_tcp->peerPort());
    return QStringLiteral("transport");
}

void SerialWorker::drainHostTxQueue() {
    QIODevice* device = activeDevice();
    if (!device || !device->isOpen() || !device->isWritable()) return;

    while (true) {
        const auto item = m_hostTx.takeNextForWrite(activeBytesToWrite());
        if (!item) break;
        const qint64 written = device->write(item->frame);

        if (written != item->frame.size()) {
            emit hostFrameWriteResult(false,
                                      written < 0 ? QStringLiteral("%1 write failed on %2").arg(item->summary, activeTransportName())
                                                  : QStringLiteral("%1 partial write: %2/%3 bytes").arg(item->summary).arg(written).arg(item->frame.size()),
                                      written > 0 ? quint64(written) : 0);
            emitHostTxQueueStatus(m_hostTx.status());
            return;
        }

        m_hostTx.markWritten();
        emit hostFrameWriteResult(true, item->summary, quint64(written));
    }

    emitHostTxQueueStatus(m_hostTx.status());
}

void SerialWorker::clearHostTxQueue(const QString& reason) {
    const auto result = m_hostTx.clear(reason);
    if (result.hadPending) emit errorOccurred(result.error);
    emitHostTxQueueStatus(result.status);
}

void SerialWorker::emitHostTxQueueStatus(const CanMonitorTransport::HostTxRuntime::Status& status) {
    emit hostTxQueueChanged(status.queuedFrames,
                            status.queuedBytes,
                            status.enqueuedFrames,
                            status.writtenFrames,
                            status.droppedFrames);
}

void SerialWorker::beginControlCycle() {
    dispatchControlCycleResult(m_controlCycle.beginCycle());
}

void SerialWorker::continueControlCycleBurst() {
    dispatchControlCycleResult(m_controlCycle.continuePacedBurst());
}

void SerialWorker::dispatchControlCycleResult(const CanMonitorControl::ControlCycleRuntime::CycleResult& result) {
    for (const QString& error : result.errors) {
        emit errorOccurred(error);
    }
    for (const auto& frame : result.frames) {
        sendHostFrame(frame.frame, frame.summary);
    }
    if (result.scheduleGap) {
        if (m_controlCycleGapTimerId != 0) killTimer(m_controlCycleGapTimerId);
        m_controlCycleGapTimerId = startTimer(result.gapMs, Qt::PreciseTimer);
    }
}
