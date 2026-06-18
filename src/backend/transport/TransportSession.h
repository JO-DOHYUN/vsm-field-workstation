#pragma once

#include <QVariantList>
#include <QString>
#include <QtGlobal>

namespace CanMonitorTransport {

class TransportSession {
public:
    struct BoardUplinkCounters {
        bool present = false;
        quint32 serialEnqueueFailTotal = 0;
        quint32 serialRingClearTotal = 0;
        quint32 serialRingClearedBytesTotal = 0;
        quint32 serialBackpressureTotal = 0;
        quint32 serialTxHighWaterBytes = 0;
        quint32 sharedCanQueueHighWater = 0;
        quint32 mcpDrainBudgetHitTotal = 0;
        quint32 canSegmentEnqueueFailTotal = 0;
    };

    void reset();
    void setConnected(bool connected);
    void updateTypedStatus(quint64 frames,
                           quint64 bytesDropped,
                           quint64 crcFailures,
                           quint64 lengthFailures,
                           quint64 versionWarnings,
                           quint64 seqGaps);
    void updateHostTxQueue(quint64 queuedFrames,
                           quint64 queuedBytes,
                           quint64 enqueuedFrames,
                           quint64 writtenFrames,
                           quint64 droppedFrames);
    void updateCaptureStorage(bool active, quint64 bytesWritten, quint64 recordCount);
    void updateBoardHealth(quint32 canDroppedTotal,
                           quint32 fifoOverflowTotal,
                           qint64 statsAgeMs,
                           const BoardUplinkCounters& uplinkCounters = BoardUplinkCounters{});
    void updateLiveRuntime(qint64 nowWallMs,
                           qint64 lastFrameWallMs,
                           qint64 lastStatsWallMs,
                           int pendingFrames,
                           quint64 sampledViewDrops,
                           quint64 projectedFrames,
                           quint64 sampledProjectionFrames,
                           quint64 droppedProjectionFrames,
                           quint64 observedControlEvidenceRecords,
                           quint64 projectedControlEvidenceRecords,
                           quint64 sampledControlEvidenceRecords,
                           int maxProjectionBacklog,
                           quint64 flushBudgetHits,
                           int lastFlushMs);
    void updateLiveTruth(quint64 observedCanRxFrames,
                         quint64 emittedTruthFrames,
                         quint64 coalescedTruthUpdates,
                         quint64 observedBus0CanRxFrames,
                         quint64 observedBus1CanRxFrames,
                         quint64 flushCount,
                         int pendingKeys,
                         int maxPendingKeys,
                         int lastInputRecords,
                         int lastOutputFrames,
                         int lastFlushMs,
                         quint64 truthLoss);
    void updateRawLedger(quint64 totalRows,
                         quint64 visibleRows,
                         quint64 segmentBytes,
                         quint64 droppedDisplayRows,
                         quint64 latestSeq);

    QString level() const;
    QString summary() const;
    QVariantList rows() const;

    quint64 parserFaultCount() const;
    quint64 hostBackpressureCount() const { return m_hostDroppedFrames; }

private:
    QString liveStateText() const;
    QString liveLevel() const;
    QString boardUplinkLevel() const;

    bool m_connected = false;
    quint64 m_typedFrames = 0;
    quint64 m_bytesDropped = 0;
    quint64 m_crcFailures = 0;
    quint64 m_lengthFailures = 0;
    quint64 m_versionWarnings = 0;
    quint64 m_seqGaps = 0;
    quint64 m_hostQueuedFrames = 0;
    quint64 m_hostQueuedBytes = 0;
    quint64 m_hostEnqueuedFrames = 0;
    quint64 m_hostWrittenFrames = 0;
    quint64 m_hostDroppedFrames = 0;
    bool m_captureActive = false;
    quint64 m_captureBytesWritten = 0;
    quint64 m_captureRecordCount = 0;
    quint32 m_boardCanDroppedTotal = 0;
    quint32 m_boardFifoOverflowTotal = 0;
    qint64 m_boardHealthAgeMs = -1;
    BoardUplinkCounters m_boardUplink;
    qint64 m_liveFrameAgeMs = -1;
    qint64 m_liveStatsAgeMs = -1;
    int m_pendingLiveFrames = 0;
    quint64 m_sampledViewDrops = 0;
    quint64 m_projectedFrames = 0;
    quint64 m_sampledProjectionFrames = 0;
    quint64 m_droppedProjectionFrames = 0;
    quint64 m_observedControlEvidenceRecords = 0;
    quint64 m_projectedControlEvidenceRecords = 0;
    quint64 m_sampledControlEvidenceRecords = 0;
    int m_maxProjectionBacklog = 0;
    quint64 m_flushBudgetHits = 0;
    int m_lastFlushMs = 0;
    quint64 m_truthObservedCanRxFrames = 0;
    quint64 m_truthEmittedFrames = 0;
    quint64 m_truthCoalescedUpdates = 0;
    quint64 m_truthObservedBus0CanRxFrames = 0;
    quint64 m_truthObservedBus1CanRxFrames = 0;
    quint64 m_truthFlushCount = 0;
    int m_truthPendingKeys = 0;
    int m_truthMaxPendingKeys = 0;
    int m_truthLastInputRecords = 0;
    int m_truthLastOutputFrames = 0;
    int m_truthLastFlushMs = 0;
    quint64 m_truthLoss = 0;
    quint64 m_rawLedgerTotalRows = 0;
    quint64 m_rawLedgerVisibleRows = 0;
    quint64 m_rawLedgerSegmentBytes = 0;
    quint64 m_rawLedgerDroppedDisplayRows = 0;
    quint64 m_rawLedgerLatestSeq = 0;
};

} // namespace CanMonitorTransport
