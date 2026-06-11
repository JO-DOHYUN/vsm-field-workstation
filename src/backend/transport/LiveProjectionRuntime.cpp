#include "transport/LiveProjectionRuntime.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr int kProjectionStatusIntervalMs = 250;
constexpr int kControlEvidenceProjectionIntervalMs = 250;
constexpr int kControlEvidenceHardPendingRecords = 64;
constexpr quint32 kControlCanIds[] = {0x503, 0x510, 0x511, 0x512, 0x513};

QVector<TypedRecord> takeSortedRecords(QHash<quint64, TypedRecord>& bucket) {
    QVector<TypedRecord> records;
    records.reserve(bucket.size());
    for (auto it = bucket.cbegin(); it != bucket.cend(); ++it) {
        records.push_back(it.value());
    }
    bucket.clear();
    std::sort(records.begin(), records.end(), [](const TypedRecord& a, const TypedRecord& b) {
        const quint64 aMono = typedRecordMonoUs(a);
        const quint64 bMono = typedRecordMonoUs(b);
        if (aMono != bMono) return aMono < bMono;
        return a.header.seq < b.header.seq;
    });
    return records;
}
}

namespace CanMonitorTransport {

LiveProjectionRuntime::LiveProjectionRuntime(int maxFramesPerBatch)
    : m_maxFramesPerBatch(std::max(1, maxFramesPerBatch)) {}

void LiveProjectionRuntime::reset() {
    m_status = {};
    m_statusTimer.invalidate();
    m_controlEvidenceTimer.invalidate();
    m_pendingAcceptedControlAckByKey.clear();
    m_pendingControlTxByKey.clear();
    m_pendingControlFeedbackRxByKey.clear();
}

LiveProjectionRuntime::IngestResult LiveProjectionRuntime::ingest(const TypedRecordList& records) {
    IngestResult result;
    result.status = m_status;
    if (records.isEmpty()) return result;

    QHash<quint64, int> frameIndexByKey;
    FrameRecordList coalescedFrames;
    coalescedFrames.reserve(std::min(int(records.size()), m_maxFramesPerBatch));

    quint64 observedCanRxInBatch = 0;
    bool sampledControlEvidenceInBatch = false;
    for (const TypedRecord& record : records) {
        if (record.isType(TypedRecordType::CanRxRaw)) {
            const auto can = decodeTypedCanRaw(record);
            if (!can) continue;

            ++m_status.observedCanRxFrames;
            ++observedCanRxInBatch;
            if (can->bus == 0) ++m_status.observedBus0CanRxFrames;
            else if (can->bus == 1) ++m_status.observedBus1CanRxFrames;

            if (isControlFeedbackCanRx(*can)) {
                ++m_status.observedControlEvidenceRecords;
                sampledControlEvidenceInBatch |= queueSampledControlEvidence(
                    m_pendingControlFeedbackRxByKey,
                    controlEvidenceKey(can->bus, can->canId),
                    record);
            }

            const quint64 key = projectionKey(*can);
            auto existing = frameIndexByKey.find(key);
            if (existing != frameIndexByKey.end()) {
                coalescedFrames[*existing] = toFrameRecord(record, *can);
            } else {
                frameIndexByKey.insert(key, coalescedFrames.size());
                coalescedFrames.push_back(toFrameRecord(record, *can));
            }
            continue;
        }

        if (record.isType(TypedRecordType::ControlAck)) {
            const auto ack = decodeTypedControlAck(record);
            if (!ack) continue;
            ++m_status.observedControlEvidenceRecords;
            if (ack->status == 0) {
                result.criticalRecords.push_back(record);
                ++m_status.projectedControlEvidenceRecords;
            } else {
                sampledControlEvidenceInBatch |= queueSampledControlEvidence(
                    m_pendingAcceptedControlAckByKey,
                    controlEvidenceKey(ack->targetBus, ack->targetCanId),
                    record);
            }
            continue;
        }

        if (record.isType(TypedRecordType::CanTxRaw)) {
            const auto can = decodeTypedCanRaw(record);
            if (!can) continue;
            ++m_status.observedControlEvidenceRecords;
            sampledControlEvidenceInBatch |= queueSampledControlEvidence(
                m_pendingControlTxByKey,
                controlEvidenceKey(can->bus, can->canId),
                record);
            continue;
        }

        if (isAlwaysCriticalRecord(record)) {
            result.criticalRecords.push_back(record);
        }
    }

    if (coalescedFrames.size() > m_maxFramesPerBatch) {
        std::sort(coalescedFrames.begin(), coalescedFrames.end(), [](const FrameRecord& a, const FrameRecord& b) {
            return a.tExtUs < b.tExtUs;
        });
        const int removeCount = coalescedFrames.size() - m_maxFramesPerBatch;
        coalescedFrames.erase(coalescedFrames.begin(), coalescedFrames.begin() + removeCount);
        m_status.workerDroppedCanRxFrames += quint64(removeCount);
    }

    std::sort(coalescedFrames.begin(), coalescedFrames.end(), [](const FrameRecord& a, const FrameRecord& b) {
        return a.tExtUs < b.tExtUs;
    });

    result.projectedFrames = coalescedFrames;
    if (controlEvidenceFlushDue()) {
        flushPendingControlEvidence(result.criticalRecords);
    }
    m_status.projectedCanRxFrames += quint64(result.projectedFrames.size());
    if (observedCanRxInBatch > quint64(result.projectedFrames.size())) {
        m_status.sampledCanRxFrames += observedCanRxInBatch - quint64(result.projectedFrames.size());
    }
    m_status.lastInputRecords = records.size();
    m_status.lastOutputFrames = result.projectedFrames.size();
    m_status.lastOutputCriticalRecords = result.criticalRecords.size();

    result.status = m_status;
    result.statusDue = statusDue(observedCanRxInBatch > quint64(result.projectedFrames.size()) ||
                                 sampledControlEvidenceInBatch);
    return result;
}

bool LiveProjectionRuntime::isAlwaysCriticalRecord(const TypedRecord& record) {
    switch (record.header.type()) {
    case TypedRecordType::BoardEvent:
    case TypedRecordType::BoardHealth:
    case TypedRecordType::Capability:
        return true;
    default:
        return false;
    }
}

bool LiveProjectionRuntime::isControlCanId(quint32 canId) {
    for (quint32 id : kControlCanIds) {
        if (canId == id) return true;
    }
    return false;
}

bool LiveProjectionRuntime::isControlFeedbackCanRx(const TypedCanRawRecord& can) {
    return isControlCanId(can.canId);
}

FrameRecord LiveProjectionRuntime::toFrameRecord(const TypedRecord& record, const TypedCanRawRecord& can) {
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

quint64 LiveProjectionRuntime::projectionKey(const TypedCanRawRecord& can) {
    return (quint64(can.bus) << 32) | quint64(can.canId);
}

quint64 LiveProjectionRuntime::controlEvidenceKey(quint8 bus, quint32 canId) {
    return (quint64(bus) << 32) | quint64(canId);
}

bool LiveProjectionRuntime::queueSampledControlEvidence(QHash<quint64, TypedRecord>& bucket,
                                                        quint64 key,
                                                        const TypedRecord& record) {
    const bool replaced = bucket.contains(key);
    if (replaced) {
        ++m_status.sampledControlEvidenceRecords;
    }
    bucket.insert(key, record);
    return replaced;
}

bool LiveProjectionRuntime::controlEvidenceFlushDue() const {
    const int pending = m_pendingAcceptedControlAckByKey.size()
        + m_pendingControlTxByKey.size()
        + m_pendingControlFeedbackRxByKey.size();
    if (pending <= 0) return false;
    if (pending >= kControlEvidenceHardPendingRecords) return true;
    return !m_controlEvidenceTimer.isValid()
        || m_controlEvidenceTimer.elapsed() >= kControlEvidenceProjectionIntervalMs;
}

void LiveProjectionRuntime::flushPendingControlEvidence(TypedRecordList& out) {
    const QVector<TypedRecord> acceptedAcks = takeSortedRecords(m_pendingAcceptedControlAckByKey);
    const QVector<TypedRecord> txAudits = takeSortedRecords(m_pendingControlTxByKey);
    const QVector<TypedRecord> feedbackFrames = takeSortedRecords(m_pendingControlFeedbackRxByKey);

    for (const TypedRecord& record : acceptedAcks) out.push_back(record);
    for (const TypedRecord& record : txAudits) out.push_back(record);
    for (const TypedRecord& record : feedbackFrames) out.push_back(record);

    m_status.projectedControlEvidenceRecords += quint64(acceptedAcks.size() + txAudits.size() + feedbackFrames.size());
    m_controlEvidenceTimer.restart();
}

bool LiveProjectionRuntime::statusDue(bool sampledThisBatch) {
    if (sampledThisBatch) return true;
    if (m_statusTimer.isValid() && m_statusTimer.elapsed() < kProjectionStatusIntervalMs) return false;
    m_statusTimer.restart();
    return true;
}

} // namespace CanMonitorTransport
