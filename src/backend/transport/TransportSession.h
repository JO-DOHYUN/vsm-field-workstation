#pragma once

#include <QHash>
#include <QJsonObject>
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
        bool hasPoolCounters = false;
        quint32 uplinkLargePoolUsedBlocks = 0;
        quint32 uplinkLargePoolCapacityBlocks = 0;
        quint32 uplinkLargePoolCanReserveUsedBlocks = 0;
        quint32 canTruthDescriptorQueueHighWater = 0;
        quint32 uplinkPoolAllocFailTotal = 0;
        quint32 canTruthPoolAllocFailTotal = 0;
        quint32 uplinkDescriptorHighWaterTotal = 0;
        quint32 diagnosticSuppressedTotal = 0;
        bool hasPassiveLifecycleCounters = false;
        quint32 hostAbsentRxDiscardBus0Total = 0;
        quint32 hostAbsentRxDiscardBus1Total = 0;
        quint32 hostAbsentFifoOverflowTotal = 0;
        quint32 hostAbsentMcpErrorTotal = 0;
        quint32 hostAbsentDurationMsTotal = 0;
        quint32 passiveReadbackTotal = 0;
        quint32 passiveReadbackViolationTotal = 0;
        quint32 txreqViolationTotal = 0;
        quint32 usbCdcDtrChangeTotal = 0;
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
    void updateLiveLatest(quint64 observedCanRxFrames,
                         quint64 emittedLatestFrames,
                         quint64 coalescedLatestUpdates,
                         quint64 observedBus0CanRxFrames,
                         quint64 observedBus1CanRxFrames,
                         quint64 flushCount,
                         int pendingKeys,
                         int maxPendingKeys,
                         int lastInputRecords,
                         int lastOutputFrames,
                         int lastFlushMs,
                         quint64 displayLoss);
    void updateRawLedger(quint64 totalRows,
                         quint64 visibleRows,
                         quint64 segmentBytes,
                         quint64 droppedDisplayRows,
                         quint64 latestSeq);
    void updateDrainPipeline(quint64 bytesTotal,
                             quint64 readyReadCount,
                             quint64 readyReadMaxUs,
                             quint64 drainBurstMaxBytes,
                             quint64 rawQueueUsedBytes,
                             quint64 rawQueueMaxUsedBytes,
                             quint64 rawQueueCapacityBytes,
                             quint64 rawQueueOverrunBytes,
                             quint64 rawQueueContentionCount,
                             quint64 parseBacklogBytes,
                             quint64 parserBatchMaxMs,
                             quint64 captureWriterQueueBytes,
                             quint64 captureWriterMaxQueueBytes,
                             quint64 captureWriterOverrunBytes,
                             quint64 captureWriteMaxMs);
    void updateAnalysisQueue(quint64 queuedFrames,
                             quint64 maxQueuedFrames,
                             quint64 capacityFrames,
                             quint64 enqueuedFrames,
                             quint64 processedFrames,
                             quint64 overrunFrames,
                             quint64 pumpCount,
                             quint64 pumpMaxMs,
                             quint64 snapshotMaxMs,
                             quint64 truthLoss);
    void updateLivePathTrace(const QJsonObject& trace);
    void updateDrainEventTrace(const QJsonObject& trace);
    void updateCoreTransportSummary(const QJsonObject& payload);
    void noteBoardEvent(quint16 code, quint16 detail, quint32 counter, quint64 monoUs);

    QString level() const;
    QString summary() const;
    QVariantList rows() const;

    quint64 parserFaultCount() const;
    quint64 hostBackpressureCount() const { return m_hostDroppedFrames; }

private:
    QString liveStateText() const;
    QString liveLevel() const;
    QString boardUplinkLevel() const;
    QString boardEventLevel() const;
    QString boardEventDetailText() const;

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
    quint64 m_liveLatestObservedCanRxFrames = 0;
    quint64 m_liveLatestEmittedFrames = 0;
    quint64 m_liveLatestCoalescedUpdates = 0;
    quint64 m_liveLatestObservedBus0CanRxFrames = 0;
    quint64 m_liveLatestObservedBus1CanRxFrames = 0;
    quint64 m_liveLatestFlushCount = 0;
    int m_liveLatestPendingKeys = 0;
    int m_liveLatestMaxPendingKeys = 0;
    int m_liveLatestLastInputRecords = 0;
    int m_liveLatestLastOutputFrames = 0;
    int m_liveLatestLastFlushMs = 0;
    quint64 m_displayLoss = 0;
    quint64 m_rawLedgerTotalRows = 0;
    quint64 m_rawLedgerVisibleRows = 0;
    quint64 m_rawLedgerSegmentBytes = 0;
    quint64 m_rawLedgerDroppedDisplayRows = 0;
    quint64 m_rawLedgerLatestSeq = 0;
    quint64 m_drainBytesTotal = 0;
    quint64 m_drainReadyReadCount = 0;
    quint64 m_drainReadyReadMaxUs = 0;
    quint64 m_drainBurstMaxBytes = 0;
    quint64 m_rawQueueUsedBytes = 0;
    quint64 m_rawQueueMaxUsedBytes = 0;
    quint64 m_rawQueueCapacityBytes = 0;
    quint64 m_rawQueueOverrunBytes = 0;
    quint64 m_rawQueueContentionCount = 0;
    quint64 m_parseBacklogBytes = 0;
    quint64 m_parserBatchMaxMs = 0;
    quint64 m_captureWriterQueueBytes = 0;
    quint64 m_captureWriterMaxQueueBytes = 0;
    quint64 m_captureWriterOverrunBytes = 0;
    quint64 m_captureWriteMaxMs = 0;
    quint64 m_analysisQueuedFrames = 0;
    quint64 m_analysisMaxQueuedFrames = 0;
    quint64 m_analysisCapacityFrames = 0;
    quint64 m_analysisEnqueuedFrames = 0;
    quint64 m_analysisProcessedFrames = 0;
    quint64 m_analysisOverrunFrames = 0;
    quint64 m_analysisPumpCount = 0;
    quint64 m_analysisPumpMaxMs = 0;
    quint64 m_analysisSnapshotMaxMs = 0;
    quint64 m_analysisTruthLoss = 0;
    QString m_runtimeProfileKey = QStringLiteral("passive_product");
    QString m_vehicleImpactState = QStringLiteral("blocked_unknown");
    QString m_serialOpenMode = QStringLiteral("read_only");
    QString m_dtrPolicy = QStringLiteral("no_touch");
    QString m_rtsPolicy = QStringLiteral("no_touch");
    bool m_dtrSessionOnly = false;
    bool m_hostTxEnabled = false;
    bool m_controlEnabled = false;
    bool m_labGatewayEnabled = false;
    quint64 m_boardEventTotal = 0;
    quint64 m_mcp2515EventTotal = 0;
    quint64 m_boardEventFatalTotal = 0;
    quint16 m_lastBoardEventCode = 0;
    quint16 m_lastBoardEventDetail = 0;
    quint32 m_lastBoardEventCounter = 0;
    quint64 m_lastBoardEventMonoUs = 0;
    QHash<quint16, quint64> m_mcp2515Details;
    QJsonObject m_livePathTrace;
    QJsonObject m_drainEventTrace;
};

} // namespace CanMonitorTransport
