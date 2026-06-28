#include "transport/LiveProjectionRuntime.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr int kProjectionStatusIntervalMs = 500;
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

QByteArray ownedPayloadForRecord(const TypedRecord& record) {
    if (record.payload.size() == record.header.payloadLength) {
        return record.payload;
    }
    const qsizetype payloadLength = qsizetype(record.header.payloadLength);
    const qsizetype frameLength = kTypedTransportFrameOverhead + payloadLength;
    if (record.frameBytes.size() >= frameLength) {
        return record.frameBytes.mid(9, payloadLength);
    }
    return record.payload;
}

CanMonitorTransport::CanRxLite toCanRxLiteFromSegmentEntry(const TypedRecord& record, const TypedCanRxSegmentEntry& entry) {
    CanMonitorTransport::CanRxLite frame;
    frame.monoUs = entry.monoUs;
    frame.canId = entry.canId;
    frame.extended = entry.extended;
    frame.rtr = entry.rtr;
    frame.dlc = entry.dlc;
    frame.bus = entry.bus;
    frame.typedSeqLsb = quint8(record.header.seq & 0xFF);
    frame.hasCaptureSeq = true;
    frame.captureSeq = entry.captureSeq;
    std::memcpy(frame.data, entry.data, sizeof(frame.data));
    return frame;
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

LiveProjectionRuntime::IngestResult LiveProjectionRuntime::ingestRecord(const TypedRecord& record) {
    IngestResult result;
    result.status = m_status;

    QHash<quint64, int> frameIndexByKey;
    QVector<CanRxLite> coalescedFrames;
    coalescedFrames.reserve(1);

    quint64 observedCanRxInBatch = 0;
    bool sampledControlEvidenceInBatch = false;
    auto processCanRx = [&](quint8 bus, quint32 canId, const quint8 data[8], const CanRxLite& frame) {
        ++m_status.observedCanRxFrames;
        ++observedCanRxInBatch;
        if (bus == 0) ++m_status.observedBus0CanRxFrames;
        else if (bus == 1) ++m_status.observedBus1CanRxFrames;

        if (isControlCanId(canId)) {
            ++m_status.observedControlEvidenceRecords;
            sampledControlEvidenceInBatch |= queueSampledControlEvidence(
                m_pendingControlFeedbackRxByKey,
                controlEvidenceKey(bus, canId),
                record);
        }

        const quint64 key = projectionKey(bus, frame.extended, frame.rtr, canId);
        auto existing = frameIndexByKey.find(key);
        if (existing != frameIndexByKey.end()) {
            coalescedFrames[*existing] = frame;
        } else {
            frameIndexByKey.insert(key, coalescedFrames.size());
            coalescedFrames.push_back(frame);
        }
        Q_UNUSED(data);
    };

    if (record.isType(TypedRecordType::CanRxRaw)) {
        const auto can = decodeTypedCanRaw(record);
        if (can) {
            processCanRx(can->bus, can->canId, can->data, toCanRxLite(record, *can));
        }
    } else if (record.isType(TypedRecordType::CanRxSegment)) {
        const auto header = decodeTypedCanRxSegmentHeader(record);
        if (header) {
            coalescedFrames.reserve(std::min<int>(header->frameCount, m_maxFramesPerBatch));
            for (qsizetype index = 0; index < header->frameCount; ++index) {
                const auto entry = decodeTypedCanRxSegmentEntry(record, index);
                if (!entry) continue;
                processCanRx(entry->bus, entry->canId, entry->data, toCanRxLiteFromSegmentEntry(record, *entry));
            }
        }
    } else if (record.isType(TypedRecordType::ControlAck)) {
        const auto ack = decodeTypedControlAck(record);
        if (ack) {
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
        }
    } else if (record.isType(TypedRecordType::CanTxRaw)) {
        const auto can = decodeTypedCanRaw(record);
        if (can) {
            ++m_status.observedControlEvidenceRecords;
            sampledControlEvidenceInBatch |= queueSampledControlEvidence(
                m_pendingControlTxByKey,
                controlEvidenceKey(can->bus, can->canId),
                record);
        }
    } else if (isAlwaysCriticalRecord(record)) {
        result.criticalRecords.push_back(record);
    }

    if (coalescedFrames.size() > m_maxFramesPerBatch) {
        std::sort(coalescedFrames.begin(), coalescedFrames.end(), [](const CanRxLite& a, const CanRxLite& b) {
            return a.monoUs < b.monoUs;
        });
        const int removeCount = coalescedFrames.size() - m_maxFramesPerBatch;
        coalescedFrames.erase(coalescedFrames.begin(), coalescedFrames.begin() + removeCount);
        m_status.workerDroppedCanRxFrames += quint64(removeCount);
    }
    std::sort(coalescedFrames.begin(), coalescedFrames.end(), [](const CanRxLite& a, const CanRxLite& b) {
        return a.monoUs < b.monoUs;
    });

    result.projectedFrames = coalescedFrames;
    if (controlEvidenceFlushDue()) {
        flushPendingControlEvidence(result.criticalRecords);
    }
    m_status.projectedCanRxFrames += quint64(result.projectedFrames.size());
    if (observedCanRxInBatch > quint64(result.projectedFrames.size())) {
        m_status.sampledCanRxFrames += observedCanRxInBatch - quint64(result.projectedFrames.size());
    }
    m_status.lastInputRecords = 1;
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

CanRxLite LiveProjectionRuntime::toCanRxLite(const TypedRecord& record, const TypedCanRawRecord& can) {
    CanRxLite frame;
    frame.monoUs = can.monoUs;
    frame.canId = can.canId;
    frame.extended = can.extended;
    frame.rtr = can.rtr;
    frame.dlc = can.dlc;
    frame.bus = can.bus;
    frame.typedSeqLsb = quint8(record.header.seq & 0xFF);
    std::memcpy(frame.data, can.data, sizeof(frame.data));
    return frame;
}

quint64 LiveProjectionRuntime::projectionKey(const TypedCanRawRecord& can) {
    return projectionKey(can.bus, can.extended, can.rtr, can.canId);
}

quint64 LiveProjectionRuntime::projectionKey(quint8 bus, bool ext, bool rtr, quint32 canId) {
    quint64 key = (quint64(bus) << 56);
    if (ext) key |= (quint64(1) << 55);
    if (rtr) key |= (quint64(1) << 54);
    key |= quint64(canId & 0x1FFFFFFFu);
    return key;
}

quint64 LiveProjectionRuntime::controlEvidenceKey(quint8 bus, quint32 canId) {
    return (quint64(bus) << 32) | quint64(canId);
}

bool LiveProjectionRuntime::queueSampledControlEvidence(QHash<quint64, TypedRecord>& bucket,
                                                        quint64 key,
                                                        const TypedRecord& record) {
    const bool replaced = bucket.contains(key);
    if (bucket.size() >= kControlEvidenceHardPendingRecords && !replaced) {
        ++m_status.sampledControlEvidenceRecords;
        return true;
    }
    if (replaced) {
        ++m_status.sampledControlEvidenceRecords;
    }
    TypedRecord compactRecord;
    compactRecord.header = record.header;
    compactRecord.payload = ownedPayloadForRecord(record);
    bucket.insert(key, std::move(compactRecord));
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

void LiveProjectionRuntime::flushPendingControlEvidence(QVector<TypedRecord>& out) {
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
    Q_UNUSED(sampledThisBatch);
    if (m_statusTimer.isValid() && m_statusTimer.elapsed() < kProjectionStatusIntervalMs) return false;
    m_statusTimer.restart();
    return true;
}

} // namespace CanMonitorTransport
