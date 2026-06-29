#pragma once

#include "../CanTypes.h"
#include "../TypedRecords.h"

#include <QElapsedTimer>
#include <QHash>

namespace CanMonitorTransport {

class LiveLatestRuntime {
public:
    struct Status {
        quint64 observedCanRxFrames = 0;
        quint64 emittedLatestFrames = 0;
        quint64 coalescedLatestUpdates = 0;
        quint64 observedBus0CanRxFrames = 0;
        quint64 observedBus1CanRxFrames = 0;
        quint64 flushCount = 0;
        int pendingKeys = 0;
        int maxPendingKeys = 0;
        int lastInputRecords = 0;
        int lastOutputFrames = 0;
        int lastFlushMs = 0;
        quint64 displayLoss = 0;
    };

    struct IngestResult {
        FrameRecordList frames;
        Status status;
        bool statusDue = false;
    };

    void reset();
    IngestResult ingestRecord(const TypedRecord& record);
    FrameRecordList flush(bool force = false);

    bool hasPending() const { return !m_pendingFramesByKey.isEmpty(); }
    bool flushDue() const;
    int flushIntervalMs() const;
    Status status() const;

private:
    static quint64 keyForFrame(const FrameRecord& frame);
    static FrameRecord toFrameRecord(const TypedRecord& record, const TypedCanRawRecord& can);
    static FrameRecord toFrameRecord(const TypedRecord& record, const TypedCanRxSegmentEntry& entry);
    void ingestFrame(const FrameRecord& frame);
    bool statusDue() const;

    QHash<quint64, FrameRecord> m_pendingFramesByKey;
    QHash<quint64, quint64> m_lastMonoUsByKey;
    QElapsedTimer m_flushClock;
    QElapsedTimer m_statusClock;
    Status m_status;
    quint64 m_lastStatusDisplayLoss = 0;
};

} // namespace CanMonitorTransport
