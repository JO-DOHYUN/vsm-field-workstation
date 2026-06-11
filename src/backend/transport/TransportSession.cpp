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

void TransportSession::updateBoardHealth(quint32 canDroppedTotal, quint32 fifoOverflowTotal, qint64 statsAgeMs) {
    m_boardCanDroppedTotal = canDroppedTotal;
    m_boardFifoOverflowTotal = fifoOverflowTotal;
    m_boardHealthAgeMs = statsAgeMs;
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

quint64 TransportSession::parserFaultCount() const {
    return m_bytesDropped + m_crcFailures + m_lengthFailures + m_versionWarnings + m_seqGaps;
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

QString TransportSession::level() const {
    if (parserFaultCount() > 0 || m_hostDroppedFrames > 0) return QStringLiteral("ERR");
    if (m_hostQueuedFrames > 64 || m_hostQueuedBytes > 16 * 1024) return QStringLiteral("WARN");
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
