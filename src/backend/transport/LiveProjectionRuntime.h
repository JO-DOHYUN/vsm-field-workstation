#pragma once

#include "../CanTypes.h"
#include "../TypedRecords.h"

#include <QElapsedTimer>
#include <QHash>
#include <QVector>

namespace CanMonitorTransport {

class LiveProjectionRuntime {
public:
    struct Status {
        quint64 observedCanRxFrames = 0;
        quint64 projectedCanRxFrames = 0;
        quint64 sampledCanRxFrames = 0;
        quint64 workerDroppedCanRxFrames = 0;
        quint64 observedBus0CanRxFrames = 0;
        quint64 observedBus1CanRxFrames = 0;
        quint64 observedControlEvidenceRecords = 0;
        quint64 projectedControlEvidenceRecords = 0;
        quint64 sampledControlEvidenceRecords = 0;
        int lastInputRecords = 0;
        int lastOutputFrames = 0;
        int lastOutputCriticalRecords = 0;
    };

    struct IngestResult {
        TypedRecordList criticalRecords;
        FrameRecordList projectedFrames;
        Status status;
        bool statusDue = false;
    };

    explicit LiveProjectionRuntime(int maxFramesPerBatch = 256);

    void reset();
    IngestResult ingest(const TypedRecordList& records);
    Status status() const { return m_status; }

private:
    static bool isAlwaysCriticalRecord(const TypedRecord& record);
    static bool isControlCanId(quint32 canId);
    static bool isControlFeedbackCanRx(const TypedCanRawRecord& can);
    static FrameRecord toFrameRecord(const TypedRecord& record, const TypedCanRawRecord& can);
    static quint64 projectionKey(const TypedCanRawRecord& can);
    static quint64 controlEvidenceKey(quint8 bus, quint32 canId);
    bool queueSampledControlEvidence(QHash<quint64, TypedRecord>& bucket, quint64 key, const TypedRecord& record);
    bool controlEvidenceFlushDue() const;
    void flushPendingControlEvidence(TypedRecordList& out);
    bool statusDue(bool sampledThisBatch);

    int m_maxFramesPerBatch = 256;
    Status m_status;
    QElapsedTimer m_statusTimer;
    QElapsedTimer m_controlEvidenceTimer;
    QHash<quint64, TypedRecord> m_pendingAcceptedControlAckByKey;
    QHash<quint64, TypedRecord> m_pendingControlTxByKey;
    QHash<quint64, TypedRecord> m_pendingControlFeedbackRxByKey;
};

} // namespace CanMonitorTransport
