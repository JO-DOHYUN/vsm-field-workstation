#include "transport/TypedEvidencePipelineWorkerRuntime.h"

#include <QDateTime>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {
constexpr qsizetype kPipelinePumpMaxBytes = 1024 * 1024;
constexpr int kPipelineStatusIntervalMs = 250;
constexpr int kRuntimeStatusSignalIntervalMs = 250;
constexpr int kProjectionSnapshotIntervalMs = 80;
constexpr int kProjectionSnapshotMaxFrames = 8;
constexpr int kProjectionSnapshotHardKeys = 64;
}

namespace CanMonitorTransport {

TypedEvidencePipelineWorkerRuntime::TypedEvidencePipelineWorkerRuntime(QSharedPointer<DrainByteQueue> queue,
                                                                       QSharedPointer<TypedRecordHandoffQueue> captureQueue,
                                                                       QObject* parent)
    : QObject(parent),
      m_queue(std::move(queue)),
      m_core(std::move(captureQueue)) {}

void TypedEvidencePipelineWorkerRuntime::reset() {
    m_core.reset();
    m_pendingProjectionByKey.clear();
    m_pumpScheduled = false;
    m_projectionSnapshotScheduled = false;
    m_projectionSnapshotInFlight = false;
    m_statusSignalScheduled = false;
    m_hasPendingProjectionStatus = false;
    m_hasPendingTruthStatus = false;
    m_hasPendingTypedStatus = false;
    m_handshakeElapsedMs = -1;
    m_statusClock.invalidate();
    m_statusSignalClock.invalidate();
    m_projectionSnapshotClock.invalidate();
    m_pendingProjectionStatus = {};
    m_pendingTruthStatus = {};
    m_pendingTypedStatus = {};
    m_projectionSnapshotEmitted = 0;
    m_projectionSnapshotCoalesced = 0;
    m_projectionSnapshotDropped = 0;
    m_livePathTelemetry = LivePathTelemetry{};
    m_eventTelemetry = DrainEventTelemetry{};
    emit pipelineStatusChanged(0, 0);
    emit captureDiagnosticsChanged(m_core.makeCaptureDiagnostics());
    emit pumpCycleFinished();
}

void TypedEvidencePipelineWorkerRuntime::schedulePump(qint64 handshakeElapsedMs) {
    m_handshakeElapsedMs = handshakeElapsedMs;
    ++m_eventTelemetry.schedulePumpCalls;
    if (m_pumpScheduled) {
        ++m_eventTelemetry.schedulePumpIgnoredAlreadyScheduled;
        return;
    }
    m_pumpScheduled = true;
    QTimer::singleShot(0, this, &TypedEvidencePipelineWorkerRuntime::pump);
}

void TypedEvidencePipelineWorkerRuntime::setCaptureEnabled(bool enabled) {
    auto options = m_core.options();
    options.captureRecords = enabled;
    m_core.setOptions(options);
}

void TypedEvidencePipelineWorkerRuntime::setSecondaryFanoutEnabled(bool enabled) {
    auto options = m_core.options();
    options.emitCanRxFrames = enabled;
    options.emitProjectionFrames = true;
    options.emitTruthFrames = enabled;
    m_core.setOptions(options);
}

void TypedEvidencePipelineWorkerRuntime::acknowledgeProjectionSnapshot() {
    m_projectionSnapshotInFlight = false;
    m_livePathTelemetry.snapshotInflight = false;
    ++m_livePathTelemetry.snapshotAck;
    if (!m_pendingProjectionByKey.isEmpty()) scheduleProjectionSnapshot();
}

void TypedEvidencePipelineWorkerRuntime::queryCoreView(const QString& viewName,
                                                       quint64 sinceSeq,
                                                       int limit,
                                                       quint64 requestId) {
    CanMonitorCore::CoreViewName parsedView = CanMonitorCore::CoreViewName::CoreHealth;
    if (!CanMonitorCore::coreViewNameFromString(viewName, &parsedView)) {
        QJsonObject error;
        error.insert(QStringLiteral("view_name"), viewName);
        error.insert(QStringLiteral("error"), QStringLiteral("unknown_core_view"));
        emit coreViewSnapshotReady(requestId, false, error, QJsonObject{});
        return;
    }

    const auto result = m_core.queryView({parsedView, sinceSeq, limit});
    emit coreViewSnapshotReady(requestId,
                               result.changed,
                               result.snapshot.toJson(),
                               result.change.toJson());
}

void TypedEvidencePipelineWorkerRuntime::pump() {
    m_pumpScheduled = false;
    ++m_eventTelemetry.pumpCalls;
    if (!m_queue) {
        emit pumpCycleFinished();
        return;
    }

    const auto snapshotBefore = m_queue->snapshot();
    const QVector<DrainByteQueue::Block> blocks = m_queue->popAll(kPipelinePumpMaxBytes);
    if (blocks.isEmpty()) {
        emitPipelineStatus(nullptr, false);
        emit pumpCycleFinished();
        return;
    }
    m_eventTelemetry.pumpBlocks += quint64(blocks.size());
    for (const auto& block : blocks) {
        m_eventTelemetry.pumpBytes += quint64(block.bytes.size());
    }

    auto result = m_core.ingestBlocks(blocks, m_handshakeElapsedMs, snapshotBefore.usedBytes);
    emitCoreViewChanges(result.viewChanges);
    if (result.capabilityFirstSeen) {
        ++m_eventTelemetry.outputSignalCount;
        emit capabilityFirstSeen(result.capabilityElapsedMs, result.capabilityBytes);
    }
    if (!result.errors.isEmpty()) {
        ++m_eventTelemetry.outputSignalCount;
        emit errorsOccurred(result.errors);
    }
    if (!result.canRxFrames.isEmpty()) {
        ++m_eventTelemetry.outputSignalCount;
        emit canRxFramesReady(result.canRxFrames);
    }
    queueProjectionSnapshotFrames(result.projectedFrames);
    if (!result.truthFrames.isEmpty()) {
        ++m_eventTelemetry.outputSignalCount;
        emit truthFramesReady(result.truthFrames);
    }
    if (!result.criticalRecords.isEmpty()) {
        ++m_eventTelemetry.outputSignalCount;
        emit criticalRecordsReady(result.criticalRecords);
    }
    if (result.projectionStatusDue) queueProjectionStatusSnapshot(result.projectionStatus);
    if (result.truthStatusDue) queueTruthStatusSnapshot(result.truthStatus);
    if (result.captureHandoffOverrun) {
        ++m_eventTelemetry.outputSignalCount;
        emit captureHandoffOverrun(result.captureHandoffOverrunRecords,
                                   result.captureHandoffOverrunBytes,
                                   result.captureHandoffError);
    }
    if (result.captureDrainNeeded) {
        ++m_eventTelemetry.outputSignalCount;
        ++m_eventTelemetry.captureQueueReadySignalCount;
        emit captureQueueReady();
    }
    if (result.typedStatusDue) queueTypedStatusSnapshot(result.typedStatus);

    emitPipelineStatus(&result, false);
    if (m_queue->snapshot().usedBytes > 0) {
        ++m_eventTelemetry.pumpRescheduleCount;
        schedulePump(m_handshakeElapsedMs);
    } else {
        const FrameRecordList finalTruth = m_core.flushTruth(true);
        if (!finalTruth.isEmpty()) {
            ++m_eventTelemetry.outputSignalCount;
            emit truthFramesReady(finalTruth);
        }
        queueTruthStatusSnapshot(m_core.truthStatus());
    }
    emit pumpCycleFinished();
}

void TypedEvidencePipelineWorkerRuntime::emitCoreViewChanges(const QVector<CanMonitorCore::ViewChanged>& changes) {
    for (const auto& change : changes) {
        ++m_eventTelemetry.outputSignalCount;
        emit coreViewChanged(change.toJson());
    }
}

void TypedEvidencePipelineWorkerRuntime::emitPipelineStatus(const CaptureCoreRuntime::Result* result, bool force) {
    const bool urgent = result && (result->captureHandoffOverrun || !result->errors.isEmpty());
    if (!force &&
        !urgent &&
        m_statusClock.isValid() &&
        m_statusClock.elapsed() < kPipelineStatusIntervalMs) {
        return;
    }

    m_statusClock.restart();
    const auto status = m_core.status();
    const auto projectionStatus = m_core.projectionStatus();
    m_livePathTelemetry.parsedCanRx = projectionStatus.observedCanRxFrames;
    m_livePathTelemetry.snapshotPendingKeys = quint64(std::max<qsizetype>(0, m_pendingProjectionByKey.size()));
    m_livePathTelemetry.snapshotEmitted = m_projectionSnapshotEmitted;
    m_livePathTelemetry.snapshotInflight = m_projectionSnapshotInFlight;
    m_livePathTelemetry.snapshotCoalesced = m_projectionSnapshotCoalesced;
    m_livePathTelemetry.snapshotDropped = m_projectionSnapshotDropped;

    m_eventTelemetry.outputSignalCount += 3;
    ++m_eventTelemetry.statusSignalCount;
    ++m_eventTelemetry.diagnosticsSignalCount;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QJsonObject liveTrace = m_livePathTelemetry.toJson(nowMs);
    QJsonObject drainTrace = m_eventTelemetry.toJson(nowMs);
    QJsonObject diagnostics = m_core.makeCaptureDiagnostics();
    QJsonObject projection;
    projection.insert(QStringLiteral("projection_snapshot_pending"), m_pendingProjectionByKey.size());
    projection.insert(QStringLiteral("projection_snapshot_emitted"), QString::number(m_projectionSnapshotEmitted));
    projection.insert(QStringLiteral("projection_snapshot_coalesced"), QString::number(m_projectionSnapshotCoalesced));
    projection.insert(QStringLiteral("projection_snapshot_dropped"), QString::number(m_projectionSnapshotDropped));
    projection.insert(QStringLiteral("qt_projection_signal_inflight"), m_projectionSnapshotInFlight);
    diagnostics.insert(QStringLiteral("live_projection_snapshot"), projection);
    diagnostics.insert(QStringLiteral("live_path_trace"), liveTrace);
    diagnostics.insert(QStringLiteral("drain_event_trace"), drainTrace);
    emit pipelineStatusChanged(status.parseBacklogBytes, status.parserBatchMaxMs);
    emit captureDiagnosticsChanged(diagnostics);
    emit pipelineTraceChanged(liveTrace, drainTrace);
}

void TypedEvidencePipelineWorkerRuntime::queueProjectionSnapshotFrames(const FrameRecordList& frames) {
    if (frames.isEmpty()) return;
    m_livePathTelemetry.projectionInputFrames += quint64(frames.size());

    for (const FrameRecord& frame : frames) {
        const quint64 key = projectionKeyForFrame(frame);
        auto existing = m_pendingProjectionByKey.find(key);
        if (existing != m_pendingProjectionByKey.end()) {
            existing.value() = frame;
            ++m_projectionSnapshotCoalesced;
            ++m_livePathTelemetry.snapshotCoalesced;
            continue;
        }
        if (m_pendingProjectionByKey.size() >= kProjectionSnapshotHardKeys) {
            ++m_projectionSnapshotDropped;
            ++m_livePathTelemetry.snapshotDropped;
            continue;
        }
        m_pendingProjectionByKey.insert(key, frame);
    }
    m_livePathTelemetry.snapshotPendingKeys = quint64(std::max<qsizetype>(0, m_pendingProjectionByKey.size()));

    scheduleProjectionSnapshot();
}

void TypedEvidencePipelineWorkerRuntime::scheduleProjectionSnapshot() {
    if (m_projectionSnapshotScheduled || m_projectionSnapshotInFlight || m_pendingProjectionByKey.isEmpty()) return;

    int delayMs = kProjectionSnapshotIntervalMs;
    if (m_projectionSnapshotClock.isValid()) {
        const int elapsed = int(m_projectionSnapshotClock.elapsed());
        delayMs = std::max(0, kProjectionSnapshotIntervalMs - elapsed);
    }

    m_projectionSnapshotScheduled = true;
    QTimer::singleShot(delayMs, this, &TypedEvidencePipelineWorkerRuntime::emitProjectionSnapshot);
}

void TypedEvidencePipelineWorkerRuntime::emitProjectionSnapshot() {
    m_projectionSnapshotScheduled = false;
    if (m_projectionSnapshotInFlight || m_pendingProjectionByKey.isEmpty()) return;

    FrameRecordList frames;
    frames.reserve(m_pendingProjectionByKey.size());
    for (auto it = m_pendingProjectionByKey.cbegin(); it != m_pendingProjectionByKey.cend(); ++it) {
        frames.push_back(it.value());
    }
    m_pendingProjectionByKey.clear();

    std::sort(frames.begin(), frames.end(), [](const FrameRecord& a, const FrameRecord& b) {
        if (a.tExtUs != b.tExtUs) return a.tExtUs < b.tExtUs;
        if (a.bus != b.bus) return a.bus < b.bus;
        return a.canId < b.canId;
    });
    if (frames.size() > kProjectionSnapshotMaxFrames) {
        const int dropCount = frames.size() - kProjectionSnapshotMaxFrames;
        m_projectionSnapshotDropped += quint64(dropCount);
        m_livePathTelemetry.snapshotDropped += quint64(dropCount);
        frames.erase(frames.begin(), frames.begin() + dropCount);
    }

    if (frames.isEmpty()) return;
    m_projectionSnapshotInFlight = true;
    ++m_projectionSnapshotEmitted;
    ++m_livePathTelemetry.snapshotEmitted;
    m_livePathTelemetry.snapshotEmittedFrames += quint64(frames.size());
    m_livePathTelemetry.snapshotPendingKeys = quint64(std::max<qsizetype>(0, m_pendingProjectionByKey.size()));
    m_livePathTelemetry.snapshotInflight = true;
    m_projectionSnapshotClock.restart();
    ++m_eventTelemetry.outputSignalCount;
    emit projectedFramesReady(frames);
}

void TypedEvidencePipelineWorkerRuntime::queueProjectionStatusSnapshot(const CanMonitorTransport::LiveProjectionRuntime::Status& status) {
    m_pendingProjectionStatus = status;
    m_hasPendingProjectionStatus = true;
    scheduleStatusSnapshotFlush();
}

void TypedEvidencePipelineWorkerRuntime::queueTruthStatusSnapshot(const CanMonitorTransport::LiveTruthRuntime::Status& status) {
    m_pendingTruthStatus = status;
    m_hasPendingTruthStatus = true;
    scheduleStatusSnapshotFlush();
}

void TypedEvidencePipelineWorkerRuntime::queueTypedStatusSnapshot(const CanMonitorTransport::TypedIngressRuntime::StatusSnapshot& status) {
    m_pendingTypedStatus = status;
    m_hasPendingTypedStatus = true;
    scheduleStatusSnapshotFlush();
}

void TypedEvidencePipelineWorkerRuntime::scheduleStatusSnapshotFlush() {
    if (m_statusSignalScheduled) return;

    int delayMs = 0;
    if (m_statusSignalClock.isValid()) {
        const int elapsed = int(m_statusSignalClock.elapsed());
        delayMs = std::max(0, kRuntimeStatusSignalIntervalMs - elapsed);
    }

    m_statusSignalScheduled = true;
    QTimer::singleShot(delayMs, this, &TypedEvidencePipelineWorkerRuntime::emitPendingStatusSnapshots);
}

void TypedEvidencePipelineWorkerRuntime::emitPendingStatusSnapshots() {
    m_statusSignalScheduled = false;
    const bool hasProjection = m_hasPendingProjectionStatus;
    const bool hasTruth = m_hasPendingTruthStatus;
    const bool hasTyped = m_hasPendingTypedStatus;
    if (!hasProjection && !hasTruth && !hasTyped) return;

    const auto projectionStatus = m_pendingProjectionStatus;
    const auto truthStatus = m_pendingTruthStatus;
    const auto typedStatus = m_pendingTypedStatus;
    m_hasPendingProjectionStatus = false;
    m_hasPendingTruthStatus = false;
    m_hasPendingTypedStatus = false;

    if (hasProjection) emitProjectionStatusSnapshot(projectionStatus);
    if (hasTruth) emitTruthStatusSnapshot(truthStatus);
    if (hasTyped) emitTypedStatusSnapshot(typedStatus);
    m_statusSignalClock.restart();

    if (m_hasPendingProjectionStatus || m_hasPendingTruthStatus || m_hasPendingTypedStatus) {
        scheduleStatusSnapshotFlush();
    }
}

void TypedEvidencePipelineWorkerRuntime::emitProjectionStatusSnapshot(const CanMonitorTransport::LiveProjectionRuntime::Status& status) {
    ++m_eventTelemetry.outputSignalCount;
    ++m_eventTelemetry.statusSignalCount;
    ++m_eventTelemetry.projectionStatusReadySignalCount;
    emit projectionStatusReady(status.observedCanRxFrames,
                               status.projectedCanRxFrames,
                               status.sampledCanRxFrames + m_projectionSnapshotCoalesced,
                               status.workerDroppedCanRxFrames + m_projectionSnapshotDropped,
                               status.observedBus0CanRxFrames,
                               status.observedBus1CanRxFrames,
                               status.observedControlEvidenceRecords,
                               status.projectedControlEvidenceRecords,
                               status.sampledControlEvidenceRecords);
}

void TypedEvidencePipelineWorkerRuntime::emitTruthStatusSnapshot(const CanMonitorTransport::LiveTruthRuntime::Status& status) {
    ++m_eventTelemetry.outputSignalCount;
    ++m_eventTelemetry.statusSignalCount;
    ++m_eventTelemetry.truthStatusReadySignalCount;
    emit truthStatusReady(status.observedCanRxFrames,
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

void TypedEvidencePipelineWorkerRuntime::emitTypedStatusSnapshot(const CanMonitorTransport::TypedIngressRuntime::StatusSnapshot& status) {
    ++m_eventTelemetry.outputSignalCount;
    ++m_eventTelemetry.statusSignalCount;
    ++m_eventTelemetry.typedStatusReadySignalCount;
    emit typedStatusReady(status.frames,
                          status.bytesDropped,
                          status.crcFailures,
                          status.lengthFailures,
                          status.versionWarnings,
                          status.seqGaps);
}

quint64 TypedEvidencePipelineWorkerRuntime::projectionKeyForFrame(const FrameRecord& frame) {
    quint64 key = (quint64(frame.bus) << 56);
    if (frame.ext) key |= (quint64(1) << 55);
    if (frame.rtr) key |= (quint64(1) << 54);
    key |= quint64(frame.canId & 0x1FFFFFFFU);
    return key;
}

} // namespace CanMonitorTransport
