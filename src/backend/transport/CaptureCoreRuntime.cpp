#include "transport/CaptureCoreRuntime.h"

#include <cstring>

namespace {
constexpr int kCoreLocalBatchSize = 128;

FrameRecord frameFromCanRawRecord(const TypedRecord& record, const TypedCanRawRecord& can) {
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

FrameRecord frameFromSegmentEntryRecord(const TypedRecord& record, const TypedCanRxSegmentEntry& entry) {
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
} // namespace

namespace CanMonitorTransport {

CaptureCoreRuntime::CaptureCoreRuntime(QSharedPointer<TypedRecordHandoffQueue> captureQueue)
    : m_captureQueue(std::move(captureQueue)) {}

void CaptureCoreRuntime::setCaptureQueue(QSharedPointer<TypedRecordHandoffQueue> queue) {
    m_captureQueue = std::move(queue);
}

void CaptureCoreRuntime::reset() {
    m_pipeline.reset();
    m_liveProjection.reset();
    m_liveTruth.reset();
}

CaptureCoreRuntime::Result CaptureCoreRuntime::ingestBlocks(const QVector<DrainByteQueue::Block>& blocks,
                                                            qint64 handshakeElapsedMs,
                                                            quint64 parseBacklogBytes) {
    Result out;
    TypedRecordList localBatch;
    localBatch.reserve(kCoreLocalBatchSize);
    auto flushLocalBatch = [this, &localBatch, &out]() {
        if (localBatch.isEmpty()) return;
        ingestBatch(std::move(localBatch), out);
        localBatch.clear();
        localBatch.reserve(kCoreLocalBatchSize);
    };

    auto result = m_pipeline.ingestBlocksEach(blocks,
                                              handshakeElapsedMs,
                                              parseBacklogBytes,
                                              [&localBatch, &flushLocalBatch](TypedRecord&& record) {
                                                  localBatch.push_back(std::move(record));
                                                  if (localBatch.size() >= kCoreLocalBatchSize) flushLocalBatch();
                                              });
    flushLocalBatch();
    out.capabilityFirstSeen = result.capabilityFirstSeen;
    out.capabilityElapsedMs = result.capabilityElapsedMs;
    out.capabilityBytes = result.capabilityBytes;
    out.errors = result.errors;
    out.typedStatusDue = result.statusDue;
    out.typedStatus = result.status;
    return out;
}

void CaptureCoreRuntime::ingestBatch(TypedRecordList&& batch, Result& result) {
    if (batch.isEmpty()) return;

    for (const TypedRecord& record : batch) {
        appendCanRxFrames(record, result.canRxFrames);
    }

    const auto projection = m_liveProjection.ingest(batch);
    if (!projection.criticalRecords.isEmpty()) {
        result.criticalRecords += projection.criticalRecords;
    }
    if (!projection.projectedFrames.isEmpty()) {
        result.projectedFrames += projection.projectedFrames;
    }
    if (projection.statusDue) {
        result.projectionStatusDue = true;
        result.projectionStatus = projection.status;
    }

    const auto truth = m_liveTruth.ingest(batch);
    if (!truth.frames.isEmpty()) {
        result.truthFrames += truth.frames;
    }
    if (truth.statusDue) {
        result.truthStatusDue = true;
        result.truthStatus = truth.status;
    }

    if (m_captureQueue) {
        auto push = m_captureQueue->push(std::move(batch));
        result.captureDrainNeeded = result.captureDrainNeeded || push.shouldScheduleDrain;
        if (!push.accepted && push.records > 0) {
            result.captureHandoffOverrun = true;
            result.captureHandoffError = push.error;
            result.captureHandoffOverrunRecords += push.records;
            result.captureHandoffOverrunBytes += push.bytes;
        }
    }
}

void CaptureCoreRuntime::appendCanRxFrames(const TypedRecord& record, FrameRecordList& out) const {
    if (record.isType(TypedRecordType::CanRxRaw)) {
        const auto can = decodeTypedCanRaw(record);
        if (can && !can->txAudit) out.push_back(frameFromCanRawRecord(record, *can));
        return;
    }
    if (!record.isType(TypedRecordType::CanRxSegment)) return;
    const auto header = decodeTypedCanRxSegmentHeader(record);
    if (!header) return;
    out.reserve(out.size() + header->frameCount);
    for (qsizetype index = 0; index < header->frameCount; ++index) {
        const auto entry = decodeTypedCanRxSegmentEntry(record, index);
        if (entry) out.push_back(frameFromSegmentEntryRecord(record, *entry));
    }
}

TypedIngressRuntime::HandshakeWatchdogState CaptureCoreRuntime::evaluateHandshake(qint64 elapsedMs,
                                                                                  qint64 timeoutMs) const {
    return m_pipeline.evaluateHandshake(elapsedMs, timeoutMs);
}

QJsonObject CaptureCoreRuntime::makeCaptureDiagnostics() const {
    return m_pipeline.makeCaptureDiagnostics();
}

FrameRecordList CaptureCoreRuntime::flushTruth(bool force) {
    return m_liveTruth.flush(force);
}

} // namespace CanMonitorTransport
