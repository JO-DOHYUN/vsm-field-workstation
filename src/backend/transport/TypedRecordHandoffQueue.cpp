#include "transport/TypedRecordHandoffQueue.h"

#include <QMutexLocker>

#include <algorithm>

namespace CanMonitorTransport {

TypedRecordHandoffQueue::TypedRecordHandoffQueue(quint64 capacityBytes)
    : m_capacityBytes(capacityBytes) {}

quint64 TypedRecordHandoffQueue::bytesForFrames(const TypedCaptureFrameList& frames) {
    quint64 bytes = 0;
    for (const TypedCaptureFrame& frame : frames) {
        bytes += quint64(frame.frameBytes.size());
    }
    return bytes;
}

TypedRecordHandoffQueue::PushResult TypedRecordHandoffQueue::push(TypedCaptureFrameList frames) {
    PushResult result;
    result.records = quint64(frames.size());
    result.bytes = bytesForFrames(frames);
    if (frames.isEmpty()) return result;

    QMutexLocker locker(&m_mutex);
    if (m_queuedBytes + result.bytes > m_capacityBytes) {
        m_overrunRecords += result.records;
        m_overrunBytes += result.bytes;
        result.error = QStringLiteral("typed capture handoff queue overrun: %1 bytes queued, %2 bytes incoming, cap %3")
            .arg(m_queuedBytes)
            .arg(result.bytes)
            .arg(m_capacityBytes);
        return result;
    }

    m_batches.push_back(std::move(frames));
    m_queuedRecords += result.records;
    m_queuedBytes += result.bytes;
    m_maxQueuedBytes = std::max(m_maxQueuedBytes, m_queuedBytes);
    ++m_pushedBatches;
    result.accepted = true;
    if (!m_drainScheduled) {
        m_drainScheduled = true;
        result.shouldScheduleDrain = true;
    }
    return result;
}

TypedCaptureFrameList TypedRecordHandoffQueue::popFrames(int maxFrames, quint64 maxBytes) {
    TypedCaptureFrameList out;
    if (maxFrames <= 0 || maxBytes == 0) return out;

    QMutexLocker locker(&m_mutex);
    quint64 outBytes = 0;
    while (!m_batches.empty() && out.size() < maxFrames) {
        TypedCaptureFrameList& front = m_batches.front();
        if (front.isEmpty()) {
            m_batches.pop_front();
            ++m_poppedBatches;
            continue;
        }

        const int remainingRecords = maxFrames - out.size();
        int take = 0;
        quint64 takeBytes = 0;
        while (take < front.size() && take < remainingRecords) {
            const quint64 recordBytes = quint64(front.at(take).frameBytes.size());
            if (take > 0 && outBytes + takeBytes + recordBytes > maxBytes) break;
            if (take == 0 && outBytes + recordBytes > maxBytes) break;
            takeBytes += recordBytes;
            ++take;
        }
        if (take <= 0) break;

        out.reserve(out.size() + take);
        for (int index = 0; index < take; ++index) {
            out.push_back(std::move(front[index]));
        }
        front.erase(front.begin(), front.begin() + take);
        m_queuedRecords -= quint64(take);
        m_queuedBytes = takeBytes > m_queuedBytes ? 0 : m_queuedBytes - takeBytes;
        outBytes += takeBytes;
        if (front.isEmpty()) {
            m_batches.pop_front();
            ++m_poppedBatches;
        }
    }

    if (m_batches.empty()) {
        m_drainScheduled = false;
    }
    return out;
}

void TypedRecordHandoffQueue::clear() {
    QMutexLocker locker(&m_mutex);
    m_batches.clear();
    m_queuedRecords = 0;
    m_queuedBytes = 0;
    m_maxQueuedBytes = 0;
    m_overrunRecords = 0;
    m_overrunBytes = 0;
    m_pushedBatches = 0;
    m_poppedBatches = 0;
    m_drainScheduled = false;
}

TypedRecordHandoffQueue::Snapshot TypedRecordHandoffQueue::snapshot() const {
    QMutexLocker locker(&m_mutex);
    Snapshot snapshot;
    snapshot.queuedRecords = m_queuedRecords;
    snapshot.queuedBytes = m_queuedBytes;
    snapshot.maxQueuedBytes = m_maxQueuedBytes;
    snapshot.capacityBytes = m_capacityBytes;
    snapshot.overrunRecords = m_overrunRecords;
    snapshot.overrunBytes = m_overrunBytes;
    snapshot.pushedBatches = m_pushedBatches;
    snapshot.poppedBatches = m_poppedBatches;
    snapshot.drainScheduled = m_drainScheduled;
    return snapshot;
}

bool TypedRecordHandoffQueue::hasQueuedRecords() const {
    QMutexLocker locker(&m_mutex);
    return m_queuedRecords > 0;
}

} // namespace CanMonitorTransport
