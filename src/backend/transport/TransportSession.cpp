#include "transport/TransportSession.h"

#include <QStringList>
#include <QVariantMap>

namespace CanMonitorTransport {

void TransportSession::reset() {
    m_connected = false;
    m_typedFrames = 0;
    m_bytesDropped = 0;
    m_crcFailures = 0;
    m_lengthFailures = 0;
    m_versionWarnings = 0;
    m_seqGaps = 0;
    m_hostQueuedFrames = 0;
    m_hostQueuedBytes = 0;
    m_hostEnqueuedFrames = 0;
    m_hostWrittenFrames = 0;
    m_hostDroppedFrames = 0;
    m_captureActive = false;
    m_captureBytesWritten = 0;
    m_captureRecordCount = 0;
    m_boardCanDroppedTotal = 0;
    m_boardFifoOverflowTotal = 0;
    m_boardHealthAgeMs = -1;
    m_boardUplink = {};
    m_liveFrameAgeMs = -1;
    m_liveStatsAgeMs = -1;
    m_pendingLiveFrames = 0;
    m_sampledViewDrops = 0;
    m_projectedFrames = 0;
    m_sampledProjectionFrames = 0;
    m_droppedProjectionFrames = 0;
    m_observedControlEvidenceRecords = 0;
    m_projectedControlEvidenceRecords = 0;
    m_sampledControlEvidenceRecords = 0;
    m_maxProjectionBacklog = 0;
    m_flushBudgetHits = 0;
    m_lastFlushMs = 0;
    m_truthObservedCanRxFrames = 0;
    m_truthEmittedFrames = 0;
    m_truthCoalescedUpdates = 0;
    m_truthObservedBus0CanRxFrames = 0;
    m_truthObservedBus1CanRxFrames = 0;
    m_truthFlushCount = 0;
    m_truthPendingKeys = 0;
    m_truthMaxPendingKeys = 0;
    m_truthLastInputRecords = 0;
    m_truthLastOutputFrames = 0;
    m_truthLastFlushMs = 0;
    m_truthLoss = 0;
    m_rawLedgerTotalRows = 0;
    m_rawLedgerVisibleRows = 0;
    m_rawLedgerSegmentBytes = 0;
    m_rawLedgerDroppedDisplayRows = 0;
    m_rawLedgerLatestSeq = 0;
}

void TransportSession::setConnected(bool connected) {
    m_connected = connected;
    if (!connected) {
        m_hostQueuedFrames = 0;
        m_hostQueuedBytes = 0;
        m_liveFrameAgeMs = -1;
        m_liveStatsAgeMs = -1;
        m_pendingLiveFrames = 0;
    }
}

void TransportSession::updateTypedStatus(quint64 frames,
                                         quint64 bytesDropped,
                                         quint64 crcFailures,
                                         quint64 lengthFailures,
                                         quint64 versionWarnings,
                                         quint64 seqGaps) {
    m_typedFrames = frames;
    m_bytesDropped = bytesDropped;
    m_crcFailures = crcFailures;
    m_lengthFailures = lengthFailures;
    m_versionWarnings = versionWarnings;
    m_seqGaps = seqGaps;
}

void TransportSession::updateHostTxQueue(quint64 queuedFrames,
                                         quint64 queuedBytes,
                                         quint64 enqueuedFrames,
                                         quint64 writtenFrames,
                                         quint64 droppedFrames) {
    m_hostQueuedFrames = queuedFrames;
    m_hostQueuedBytes = queuedBytes;
    m_hostEnqueuedFrames = enqueuedFrames;
    m_hostWrittenFrames = writtenFrames;
    m_hostDroppedFrames = droppedFrames;
}

void TransportSession::updateCaptureStorage(bool active, quint64 bytesWritten, quint64 recordCount) {
    m_captureActive = active;
    m_captureBytesWritten = bytesWritten;
    m_captureRecordCount = recordCount;
}

void TransportSession::updateBoardHealth(quint32 canDroppedTotal,
                                         quint32 fifoOverflowTotal,
                                         qint64 statsAgeMs,
                                         const BoardUplinkCounters& uplinkCounters) {
    m_boardCanDroppedTotal = canDroppedTotal;
    m_boardFifoOverflowTotal = fifoOverflowTotal;
    m_boardHealthAgeMs = statsAgeMs;
    m_boardUplink = uplinkCounters;
}

void TransportSession::updateLiveRuntime(qint64 nowWallMs,
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
                                         int lastFlushMs) {
    m_liveFrameAgeMs = lastFrameWallMs > 0 ? nowWallMs - lastFrameWallMs : -1;
    m_liveStatsAgeMs = lastStatsWallMs > 0 ? nowWallMs - lastStatsWallMs : -1;
    m_pendingLiveFrames = pendingFrames;
    m_sampledViewDrops = sampledViewDrops;
    m_projectedFrames = projectedFrames;
    m_sampledProjectionFrames = sampledProjectionFrames;
    m_droppedProjectionFrames = droppedProjectionFrames;
    m_observedControlEvidenceRecords = observedControlEvidenceRecords;
    m_projectedControlEvidenceRecords = projectedControlEvidenceRecords;
    m_sampledControlEvidenceRecords = sampledControlEvidenceRecords;
    m_maxProjectionBacklog = maxProjectionBacklog;
    m_flushBudgetHits = flushBudgetHits;
    m_lastFlushMs = lastFlushMs;
}

void TransportSession::updateLiveTruth(quint64 observedCanRxFrames,
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
                                       quint64 truthLoss) {
    m_truthObservedCanRxFrames = observedCanRxFrames;
    m_truthEmittedFrames = emittedTruthFrames;
    m_truthCoalescedUpdates = coalescedTruthUpdates;
    m_truthObservedBus0CanRxFrames = observedBus0CanRxFrames;
    m_truthObservedBus1CanRxFrames = observedBus1CanRxFrames;
    m_truthFlushCount = flushCount;
    m_truthPendingKeys = pendingKeys;
    m_truthMaxPendingKeys = maxPendingKeys;
    m_truthLastInputRecords = lastInputRecords;
    m_truthLastOutputFrames = lastOutputFrames;
    m_truthLastFlushMs = lastFlushMs;
    m_truthLoss = truthLoss;
}

void TransportSession::updateRawLedger(quint64 totalRows,
                                       quint64 visibleRows,
                                       quint64 segmentBytes,
                                       quint64 droppedDisplayRows,
                                       quint64 latestSeq) {
    m_rawLedgerTotalRows = totalRows;
    m_rawLedgerVisibleRows = visibleRows;
    m_rawLedgerSegmentBytes = segmentBytes;
    m_rawLedgerDroppedDisplayRows = droppedDisplayRows;
    m_rawLedgerLatestSeq = latestSeq;
}

quint64 TransportSession::parserFaultCount() const {
    return m_bytesDropped + m_crcFailures + m_lengthFailures + m_versionWarnings + m_seqGaps + m_truthLoss;
}

QString TransportSession::liveLevel() const {
    if (!m_connected) return QStringLiteral("INFO");
    if (m_liveFrameAgeMs >= 0 && m_liveFrameAgeMs <= 1200) {
        if (m_droppedProjectionFrames > 0) return QStringLiteral("WARN");
        if (m_pendingLiveFrames > 4096 || m_sampledViewDrops > 0 || m_sampledProjectionFrames > 0) return QStringLiteral("WARN");
        return QStringLiteral("OK");
    }
    if (m_liveStatsAgeMs >= 0 && m_liveStatsAgeMs <= 2000) return QStringLiteral("WARN");
    return QStringLiteral("WARN");
}

QString TransportSession::boardUplinkLevel() const {
    if (!m_boardUplink.present) return QStringLiteral("INFO");
    if (m_boardUplink.serialRingClearTotal > 0 ||
        m_boardUplink.serialEnqueueFailTotal > 0 ||
        m_boardUplink.canSegmentEnqueueFailTotal > 0) {
        return QStringLiteral("ERR");
    }
    if (m_boardUplink.serialBackpressureTotal > 0 ||
        m_boardUplink.mcpDrainBudgetHitTotal > 0) {
        return QStringLiteral("WARN");
    }
    return QStringLiteral("OK");
}

QString TransportSession::level() const {
    if (parserFaultCount() > 0 || m_hostDroppedFrames > 0) return QStringLiteral("ERR");
    if (boardUplinkLevel() == QStringLiteral("ERR")) return QStringLiteral("ERR");
    if (m_hostQueuedFrames > 64 || m_hostQueuedBytes > 16 * 1024) return QStringLiteral("WARN");
    if (boardUplinkLevel() == QStringLiteral("WARN")) return QStringLiteral("WARN");
    if (liveLevel() == QStringLiteral("WARN")) return QStringLiteral("WARN");
    return QStringLiteral("OK");
}

QString TransportSession::liveStateText() const {
    if (!m_connected) return QStringLiteral("not connected");
    if (m_liveFrameAgeMs >= 0 && m_liveFrameAgeMs <= 1200) return QStringLiteral("frames live age %1ms").arg(m_liveFrameAgeMs);
    if (m_liveStatsAgeMs >= 0 && m_liveStatsAgeMs <= 2000) return QStringLiteral("stats live age %1ms, frame delayed").arg(m_liveStatsAgeMs);
    return QStringLiteral("connected but no recent live frame");
}

QString TransportSession::summary() const {
    QStringList parts;
    parts << QStringLiteral("transport %1").arg(level());
    parts << QStringLiteral("typed frames %1 faults %2").arg(m_typedFrames).arg(parserFaultCount());
    parts << QStringLiteral("hostTX q %1/%2B written %3 drop %4")
                 .arg(m_hostQueuedFrames)
                 .arg(m_hostQueuedBytes)
                 .arg(m_hostWrittenFrames)
                 .arg(m_hostDroppedFrames);
    parts << liveStateText();
    if (m_pendingLiveFrames > 0) parts << QStringLiteral("pending live %1").arg(m_pendingLiveFrames);
    if (m_sampledProjectionFrames > 0 || m_sampledViewDrops > 0 || m_sampledControlEvidenceRecords > 0) {
        parts << QStringLiteral("projection sampled %1 view %2 control %3")
                     .arg(m_sampledProjectionFrames)
                     .arg(m_sampledViewDrops)
                     .arg(m_sampledControlEvidenceRecords);
    }
    if (m_droppedProjectionFrames > 0) parts << QStringLiteral("projection dropped %1").arg(m_droppedProjectionFrames);
    if (m_boardUplink.present) {
        parts << QStringLiteral("csm uplink clear %1 bp %2 seg_fail %3")
                     .arg(m_boardUplink.serialRingClearTotal)
                     .arg(m_boardUplink.serialBackpressureTotal)
                     .arg(m_boardUplink.canSegmentEnqueueFailTotal);
    }
    if (m_rawLedgerTotalRows > 0) parts << QStringLiteral("raw ledger %1 rows").arg(m_rawLedgerTotalRows);
    return parts.join(QStringLiteral(" | "));
}

QVariantList TransportSession::rows() const {
    auto row = [](const QString& key,
                  const QString& title,
                  const QString& level,
                  const QString& value,
                  const QString& detail,
                  bool blocking) {
        QVariantMap out;
        out.insert(QStringLiteral("key"), key);
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("level"), level);
        out.insert(QStringLiteral("value"), value);
        out.insert(QStringLiteral("detail"), detail);
        out.insert(QStringLiteral("blocking"), blocking);
        return out;
    };

    const quint64 parserFaults = parserFaultCount();
    const QString parserLevel = parserFaults > 0 ? QStringLiteral("ERR") : QStringLiteral("OK");
    const QString hostLevel = m_hostDroppedFrames > 0 ? QStringLiteral("ERR")
        : (m_hostQueuedFrames > 64 || m_hostQueuedBytes > 16 * 1024 ? QStringLiteral("WARN") : QStringLiteral("OK"));
    const QString captureLevel = m_captureActive || m_captureRecordCount > 0 ? QStringLiteral("OK") : QStringLiteral("INFO");
    const QString boardLevel = (m_boardCanDroppedTotal > 0 || m_boardFifoOverflowTotal > 0) ? QStringLiteral("ERR")
        : (m_boardHealthAgeMs >= 0 && m_boardHealthAgeMs <= 2000 ? QStringLiteral("OK") : QStringLiteral("WARN"));
    const QString projectionLevel = m_droppedProjectionFrames > 0 ? QStringLiteral("WARN")
        : (m_sampledProjectionFrames > 0 || m_sampledViewDrops > 0 || m_pendingLiveFrames > 4096 ? QStringLiteral("WARN") : QStringLiteral("OK"));
    const QString truthLevel = m_truthLoss > 0 ? QStringLiteral("ERR")
        : (m_truthPendingKeys > 4096 ? QStringLiteral("WARN") : QStringLiteral("OK"));
    const QString rawLedgerLevel = m_rawLedgerDroppedDisplayRows > 0 ? QStringLiteral("WARN") : QStringLiteral("OK");
    const QString uplinkLevel = boardUplinkLevel();

    return QVariantList{
        row(QStringLiteral("capture_storage"),
            QStringLiteral("Capture storage"),
            captureLevel,
            m_captureActive ? QStringLiteral("recording") : QStringLiteral("idle/finalized"),
            QStringLiteral("bytes %1 records %2").arg(m_captureBytesWritten).arg(m_captureRecordCount),
            false),
        row(QStringLiteral("typed_parser"),
            QStringLiteral("Typed parser"),
            parserLevel,
            QStringLiteral("frames %1 faults %2").arg(m_typedFrames).arg(parserFaults),
            QStringLiteral("drop %1 crc %2 len %3 seq %4 ver %5")
                .arg(m_bytesDropped)
                .arg(m_crcFailures)
                .arg(m_lengthFailures)
                .arg(m_seqGaps)
                .arg(m_versionWarnings),
            parserFaults > 0),
        row(QStringLiteral("host_tx_queue"),
            QStringLiteral("Host TX queue"),
            hostLevel,
            QStringLiteral("queued %1 / %2 bytes").arg(m_hostQueuedFrames).arg(m_hostQueuedBytes),
            QStringLiteral("enqueued %1 written %2 dropped %3")
                .arg(m_hostEnqueuedFrames)
                .arg(m_hostWrittenFrames)
                .arg(m_hostDroppedFrames),
            m_hostDroppedFrames > 0),
        row(QStringLiteral("board_health"),
            QStringLiteral("Board health"),
            boardLevel,
            m_boardHealthAgeMs >= 0 ? QStringLiteral("health age %1ms").arg(m_boardHealthAgeMs) : QStringLiteral("no health yet"),
            QStringLiteral("can_drop %1 fifo_overflow %2").arg(m_boardCanDroppedTotal).arg(m_boardFifoOverflowTotal),
            m_boardCanDroppedTotal > 0 || m_boardFifoOverflowTotal > 0),
        row(QStringLiteral("csm_uplink"),
            QStringLiteral("CSM uplink"),
            uplinkLevel,
            m_boardUplink.present
                ? QStringLiteral("ring_clear %1 backpressure %2")
                      .arg(m_boardUplink.serialRingClearTotal)
                      .arg(m_boardUplink.serialBackpressureTotal)
                : QStringLiteral("extended health counters unavailable"),
            m_boardUplink.present
                ? QStringLiteral("cleared_bytes %1 enqueue_fail %2 can_segment_enqueue_fail %3 serial_high_water %4 shared_queue_high %5 mcp_budget_hits %6")
                      .arg(m_boardUplink.serialRingClearedBytesTotal)
                      .arg(m_boardUplink.serialEnqueueFailTotal)
                      .arg(m_boardUplink.canSegmentEnqueueFailTotal)
                      .arg(m_boardUplink.serialTxHighWaterBytes)
                      .arg(m_boardUplink.sharedCanQueueHighWater)
                      .arg(m_boardUplink.mcpDrainBudgetHitTotal)
                : QStringLiteral("BOARD_HEALTH payload is legacy 52B; cannot separate USB backpressure from board CAN counters"),
            m_boardUplink.present &&
                (m_boardUplink.serialRingClearTotal > 0 ||
                 m_boardUplink.serialEnqueueFailTotal > 0 ||
                 m_boardUplink.canSegmentEnqueueFailTotal > 0)),
        row(QStringLiteral("live_truth"),
            QStringLiteral("Live truth"),
            truthLevel,
            QStringLiteral("observed %1 emitted %2 pending %3")
                .arg(m_truthObservedCanRxFrames)
                .arg(m_truthEmittedFrames)
                .arg(m_truthPendingKeys),
            QStringLiteral("bus0 %1 bus1 %2 coalesced_snapshot_updates %3 flushes %4 max_pending %5 last_in %6 last_out %7 flush_ms %8 truth_loss %9")
                .arg(m_truthObservedBus0CanRxFrames)
                .arg(m_truthObservedBus1CanRxFrames)
                .arg(m_truthCoalescedUpdates)
                .arg(m_truthFlushCount)
                .arg(m_truthMaxPendingKeys)
                .arg(m_truthLastInputRecords)
                .arg(m_truthLastOutputFrames)
                .arg(m_truthLastFlushMs)
                .arg(m_truthLoss),
            m_truthLoss > 0),
        row(QStringLiteral("raw_ledger"),
            QStringLiteral("Raw ledger"),
            rawLedgerLevel,
            QStringLiteral("rows %1 visible %2 latest %3")
                .arg(m_rawLedgerTotalRows)
                .arg(m_rawLedgerVisibleRows)
                .arg(m_rawLedgerLatestSeq),
            QStringLiteral("segment_bytes %1 display_dropped %2 truth_preserved %3")
                .arg(m_rawLedgerSegmentBytes)
                .arg(m_rawLedgerDroppedDisplayRows)
                .arg(m_rawLedgerDroppedDisplayRows == 0 ? QStringLiteral("yes") : QStringLiteral("check")),
            false),
        row(QStringLiteral("live_projection"),
            QStringLiteral("Live projection"),
            projectionLevel,
            QStringLiteral("pending %1 projected %2").arg(m_pendingLiveFrames).arg(m_projectedFrames),
            QStringLiteral("sampled %1 view %2 dropped %3 control %4/%5 sampled %6 max_backlog %7 budget_hits %8 last_flush_ms %9")
                .arg(m_sampledProjectionFrames)
                .arg(m_sampledViewDrops)
                .arg(m_droppedProjectionFrames)
                .arg(m_projectedControlEvidenceRecords)
                .arg(m_observedControlEvidenceRecords)
                .arg(m_sampledControlEvidenceRecords)
                .arg(m_maxProjectionBacklog)
                .arg(m_flushBudgetHits)
                .arg(m_lastFlushMs),
            false),
        row(QStringLiteral("live_delay"),
            QStringLiteral("Live delay"),
            liveLevel(),
            liveStateText(),
            QStringLiteral("frame_age %1 stats_age %2").arg(m_liveFrameAgeMs).arg(m_liveStatsAgeMs),
            false)
    };
}

} // namespace CanMonitorTransport
