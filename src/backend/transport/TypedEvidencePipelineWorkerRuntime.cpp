#include "transport/TypedEvidencePipelineWorkerRuntime.h"

#include <QTimer>

namespace {
constexpr qsizetype kPipelinePumpMaxBytes = 1024 * 1024;
}

namespace CanMonitorTransport {

TypedEvidencePipelineWorkerRuntime::TypedEvidencePipelineWorkerRuntime(QSharedPointer<DrainByteQueue> queue,
                                                                       QObject* parent)
    : QObject(parent),
      m_queue(std::move(queue)) {}

void TypedEvidencePipelineWorkerRuntime::reset() {
    m_pipeline.reset();
    m_pumpScheduled = false;
    m_handshakeElapsedMs = -1;
    emit pipelineStatusChanged(0, 0);
    emit captureDiagnosticsChanged(m_pipeline.makeCaptureDiagnostics());
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
        const auto status = m_pipeline.status();
        emit pipelineStatusChanged(status.parseBacklogBytes, status.parserBatchMaxMs);
        return;
    }

    const auto result = m_pipeline.ingestBlocks(blocks, m_handshakeElapsedMs, snapshotBefore.usedBytes);
    if (result.capabilityFirstSeen) {
        emit capabilityFirstSeen(result.capabilityElapsedMs, result.capabilityBytes);
    }
    if (!result.errors.isEmpty()) emit errorsOccurred(result.errors);
    for (const TypedRecordList& batch : result.recordBatches) {
        if (!batch.isEmpty()) emit recordBatchReady(batch);
    }
    if (result.statusDue) {
        const auto& s = result.status;
        emit typedStatusReady(s.frames,
                              s.bytesDropped,
                              s.crcFailures,
                              s.lengthFailures,
                              s.versionWarnings,
                              s.seqGaps);
    }

    const auto status = m_pipeline.status();
    emit pipelineStatusChanged(status.parseBacklogBytes, status.parserBatchMaxMs);
    emit captureDiagnosticsChanged(m_pipeline.makeCaptureDiagnostics());
    if (m_queue->snapshot().usedBytes > 0) schedulePump(m_handshakeElapsedMs);
}

} // namespace CanMonitorTransport
