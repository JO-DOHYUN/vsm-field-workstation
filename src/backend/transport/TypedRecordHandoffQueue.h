#pragma once

#include "../TypedRecords.h"

#include <QMutex>
#include <QString>

#include <deque>

namespace CanMonitorTransport {

class TypedRecordHandoffQueue {
public:
    struct PushResult {
        bool accepted = false;
        bool shouldScheduleDrain = false;
        quint64 records = 0;
        quint64 bytes = 0;
        QString error;
    };

    struct Snapshot {
        quint64 queuedRecords = 0;
        quint64 queuedBytes = 0;
        quint64 maxQueuedBytes = 0;
        quint64 capacityBytes = 0;
        quint64 overrunRecords = 0;
        quint64 overrunBytes = 0;
        quint64 pushedBatches = 0;
        quint64 poppedBatches = 0;
        bool drainScheduled = false;
    };

    explicit TypedRecordHandoffQueue(quint64 capacityBytes = 16ULL * 1024ULL * 1024ULL);

    PushResult push(TypedRecordList records);
    TypedRecordList popRecords(int maxRecords, quint64 maxBytes);
    void clear();

    Snapshot snapshot() const;
    bool hasQueuedRecords() const;

private:
    static quint64 bytesForRecords(const TypedRecordList& records);

    mutable QMutex m_mutex;
    std::deque<TypedRecordList> m_batches;
    quint64 m_capacityBytes = 0;
    quint64 m_queuedRecords = 0;
    quint64 m_queuedBytes = 0;
    quint64 m_maxQueuedBytes = 0;
    quint64 m_overrunRecords = 0;
    quint64 m_overrunBytes = 0;
    quint64 m_pushedBatches = 0;
    quint64 m_poppedBatches = 0;
    bool m_drainScheduled = false;
};

} // namespace CanMonitorTransport
