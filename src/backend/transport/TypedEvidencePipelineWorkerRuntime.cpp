#include "transport/TypedEvidencePipelineWorkerRuntime.h"

#include <QTimer>

#include <utility>

namespace {
constexpr qsizetype kPipelinePumpMaxBytes = 1024 * 1024;
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
    m_pumpScheduled = false;
    m_handshakeElapsedMs = -1;
    emit pipelineStatusChanged(0, 0);
    emit captureDiagnosticsChanged(m_core.makeCaptureDiagnostics());
}

void TypedEvidencePipelineWorkerRuntime::schedulePump(qint64 handshakeElapsedMs) {
    m_handshakeElapsedMs = handshakeElapsedMs;
    if (m_pumpScheduled) return;
    m_pumpScheduled = true;
    QTimer::singleShot(0, this, &TypedEvidencePipelineWorkerRuntime::pump);
}

void TypedEvidencePipelineWorkerRuntime::pump() {
    m_pumpScheduled = false;
    if (!m_queue) return;

    const auto snapshotBefore = m_queue->snapshot();
    const QVector<DrainByteQueue::Block> blocks = m_queue->popAll(kPipelinePumpMaxBytes);
    if (blocks.isEmpty()) {
        const auto status = m_core.status();
        emit pipelineStatusChanged(status.parseBacklogBytes, status.parserBatchMaxMs);
        return;
    }

    auto result = m_core.ingestBlocks(blocks, m_handshakeElapsedMs, snapshotBefore.usedBytes);
    if (result.capabilityFirstSeen) {
        emit capabilityFirstSeen(result.capabilityElapsedMs, result.capabilityBytes);
    }
    if (!result.errors.isEmpty()) emit errorsOccurred(result.errors);
    if (!result.canRxFrames.isEmpty()) emit canRxFramesReady(result.canRxFrames);
    if (!result.projectedFrames.isEmpty()) emit projectedFramesReady(result.projectedFrames);
    if (!result.truthFrames.isEmpty()) emit truthFramesReady(result.truthFrames);
    if (!result.criticalRecords.isEmpty()) emit criticalRecordsReady(result.criticalRecords);
    if (result.projectionStatusDue) {
        const auto& s = result.projectionStatus;
        emit projectionStatusReady(s.observedCanRxFrames,
                                   s.projectedCanRxFrames,
                                   s.sampledCanRxFrames,
                                   s.workerDroppedCanRxFrames,
                                   s.observedBus0CanRxFrames,
                                   s.observedBus1CanRxFrames,
                                   s.observedControlEvidenceRecords,
                                   s.projectedControlEvidenceRecords,
                                   s.sampledControlEvidenceRecords);
    }
    if (result.truthStatusDue) {
        const auto& s = result.truthStatus;
        emit truthStatusReady(s.observedCanRxFrames,
                              s.emittedTruthFrames,
                              s.coalescedTruthUpdates,
                              s.observedBus0CanRxFrames,
                              s.observedBus1CanRxFrames,
                              s.flushCount,
                              s.pendingKeys,
                              s.maxPendingKeys,
                              s.lastInputRecords,
                              s.lastOutputFrames,
                              s.lastFlushMs,
                              s.truthLoss);
    }
    if (result.captureHandoffOverrun) {
        emit captureHandoffOverrun(result.captureHandoffOverrunRecords,
                                   result.captureHandoffOverrunBytes,
                                   result.captureHandoffError);
    }
    if (result.captureDrainNeeded) emit captureQueueReady();
    if (result.typedStatusDue) {
        const auto& s = result.typedStatus;
        emit typedStatusReady(s.frames,
                              s.bytesDropped,
                              s.crcFailures,
                              s.lengthFailures,
                              s.versionWarnings,
                              s.seqGaps);
    }

    const auto status = m_core.status();
    emit pipelineStatusChanged(status.parseBacklogBytes, status.parserBatchMaxMs);
    emit captureDiagnosticsChanged(m_core.makeCaptureDiagnostics());
    if (m_queue->snapshot().usedBytes > 0) {
        schedulePump(m_handshakeElapsedMs);
    } else {
        const FrameRecordList finalTruth = m_core.flushTruth(true);
        if (!finalTruth.isEmpty()) emit truthFramesReady(finalTruth);
        const auto truthStatus = m_core.truthStatus();
        emit truthStatusReady(truthStatus.observedCanRxFrames,
                              truthStatus.emittedTruthFrames,
                              truthStatus.coalescedTruthUpdates,
                              truthStatus.observedBus0CanRxFrames,
                              truthStatus.observedBus1CanRxFrames,
                              truthStatus.flushCount,
                              truthStatus.pendingKeys,
                              truthStatus.maxPendingKeys,
                              truthStatus.lastInputRecords,
                              truthStatus.lastOutputFrames,
                              truthStatus.lastFlushMs,
                              truthStatus.truthLoss);
    }
}

} // namespace CanMonitorTransport
