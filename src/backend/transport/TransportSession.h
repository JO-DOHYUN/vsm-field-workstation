#pragma once

#include <QVariantList>
#include <QString>
#include <QtGlobal>

namespace CanMonitorTransport {

class TransportSession {
public:
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
    void updateBoardHealth(quint32 canDroppedTotal, quint32 fifoOverflowTotal, qint64 statsAgeMs);
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

    QString level() const;
    QString summary() const;
    QVariantList rows() const;

    quint64 parserFaultCount() const;
    quint64 hostBackpressureCount() const { return m_hostDroppedFrames; }

private:
    QString liveStateText() const;
    QString liveLevel() const;

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
};

} // namespace CanMonitorTransport
