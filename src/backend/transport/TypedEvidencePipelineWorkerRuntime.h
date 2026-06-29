#pragma once

#include "transport/CoreDataBatches.h"
#include "transport/CaptureCoreRuntime.h"
#include "transport/DrainByteQueue.h"
#include "transport/LivePathTelemetry.h"
#include "transport/TypedRecordHandoffQueue.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSharedPointer>
#include <QStringList>

namespace CanMonitorTransport {

class TypedEvidencePipelineWorkerRuntime : public QObject {
    Q_OBJECT
public:
    explicit TypedEvidencePipelineWorkerRuntime(QSharedPointer<DrainByteQueue> queue,
                                                QSharedPointer<TypedRecordHandoffQueue> captureQueue = {},
                                                QObject* parent = nullptr);

public slots:
    void reset();
    void schedulePump(qint64 handshakeElapsedMs);
    void setCaptureEnabled(bool enabled);
    void setCanRxFramesEnabled(bool enabled);
    void setSecondaryFanoutEnabled(bool enabled);
    void queryCoreView(const QString& viewName, quint64 sinceSeq, int limit, quint64 requestId);

signals:
    void capabilityFirstSeen(qint64 elapsedMs, quint64 bytes);
    void errorsOccurred(const QStringList& errors);
    void analysisFramesReady(const CanMonitorTransport::AnalysisFrameBatch& batch);
    void rawLedgerFramesReady(const CanMonitorTransport::RawLedgerFrameBatch& batch);
    void captureQueueReady();
    void captureHandoffOverrun(quint64 records, quint64 bytes, const QString& reason);
    void projectionStatusReady(quint64 observedCanRxFrames,
                               quint64 projectedCanRxFrames,
                               quint64 sampledCanRxFrames,
                               quint64 workerDroppedCanRxFrames,
                               quint64 observedBus0CanRxFrames,
                               quint64 observedBus1CanRxFrames,
                               quint64 observedControlEvidenceRecords,
                               quint64 projectedControlEvidenceRecords,
                               quint64 sampledControlEvidenceRecords);
    void liveLatestStatusReady(quint64 observedCanRxFrames,
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
    void typedStatusReady(quint64 frames,
                          quint64 bytesDropped,
                          quint64 crcFailures,
                          quint64 lengthFailures,
                          quint64 versionWarnings,
                          quint64 seqGaps);
    void pipelineStatusChanged(quint64 parseBacklogBytes, quint64 parserBatchMaxMs);
    void captureDiagnosticsChanged(const QJsonObject& diagnostics);
    void pipelineTraceChanged(const QJsonObject& livePathTrace, const QJsonObject& drainEventTrace);
    void coreViewChanged(const QJsonObject& change);
    void coreViewSnapshotReady(quint64 requestId, bool changed, const QJsonObject& snapshot, const QJsonObject& change);
    void pumpCycleFinished();

private slots:
    void pump();
    void emitPendingStatusSnapshots();

private:
    void emitPipelineStatus(const CaptureCoreRuntime::Result* result = nullptr, bool force = false);
    void emitCoreViewChanges(const QVector<CanMonitorCore::ViewChanged>& changes);
    void queueProjectionStatusSnapshot(const CanMonitorTransport::LiveProjectionRuntime::Status& status);
    void queueLiveLatestStatusSnapshot(const CanMonitorTransport::LiveLatestRuntime::Status& status);
    void queueTypedStatusSnapshot(const CanMonitorTransport::TypedIngressRuntime::StatusSnapshot& status);
    void scheduleStatusSnapshotFlush();
    void emitProjectionStatusSnapshot(const CanMonitorTransport::LiveProjectionRuntime::Status& status);
    void emitLiveLatestStatusSnapshot(const CanMonitorTransport::LiveLatestRuntime::Status& status);
    void emitTypedStatusSnapshot(const CanMonitorTransport::TypedIngressRuntime::StatusSnapshot& status);
    static quint64 projectionKeyForFrame(const FrameRecord& frame);

    QSharedPointer<DrainByteQueue> m_queue;
    CaptureCoreRuntime m_core;
    LivePathTelemetry m_livePathTelemetry;
    DrainEventTelemetry m_eventTelemetry;
    QElapsedTimer m_statusClock;
    QElapsedTimer m_statusSignalClock;
    LiveProjectionRuntime::Status m_pendingProjectionStatus;
    LiveLatestRuntime::Status m_pendingLiveLatestStatus;
    TypedIngressRuntime::StatusSnapshot m_pendingTypedStatus;
    bool m_pumpScheduled = false;
    bool m_statusSignalScheduled = false;
    bool m_hasPendingProjectionStatus = false;
    bool m_hasPendingLiveLatestStatus = false;
    bool m_hasPendingTypedStatus = false;
    qint64 m_handshakeElapsedMs = -1;
};

} // namespace CanMonitorTransport
