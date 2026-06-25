#pragma once

#include "transport/CaptureCoreRuntime.h"
#include "transport/DrainByteQueue.h"
#include "transport/TypedRecordHandoffQueue.h"

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

signals:
    void capabilityFirstSeen(qint64 elapsedMs, quint64 bytes);
    void errorsOccurred(const QStringList& errors);
    void canRxFramesReady(const FrameRecordList& frames);
    void projectedFramesReady(const FrameRecordList& frames);
    void truthFramesReady(const FrameRecordList& frames);
    void criticalRecordsReady(const TypedRecordList& records);
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
    void truthStatusReady(quint64 observedCanRxFrames,
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
    void typedStatusReady(quint64 frames,
                          quint64 bytesDropped,
                          quint64 crcFailures,
                          quint64 lengthFailures,
                          quint64 versionWarnings,
                          quint64 seqGaps);
    void pipelineStatusChanged(quint64 parseBacklogBytes, quint64 parserBatchMaxMs);
    void captureDiagnosticsChanged(const QJsonObject& diagnostics);

private slots:
    void pump();

private:
    QSharedPointer<DrainByteQueue> m_queue;
    CaptureCoreRuntime m_core;
    bool m_pumpScheduled = false;
    qint64 m_handshakeElapsedMs = -1;
};

} // namespace CanMonitorTransport
