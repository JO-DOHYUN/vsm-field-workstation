#pragma once

#include "core/CoreMaterializedViewStore.h"
#include "transport/CoreDataBatches.h"
#include "transport/LiveProjectionRuntime.h"
#include "transport/LiveTruthRuntime.h"
#include "transport/TypedEvidencePipelineRuntime.h"
#include "transport/TypedRecordHandoffQueue.h"

#include <QHash>
#include <QSharedPointer>
#include <QVector>

namespace CanMonitorTransport {

class CaptureCoreRuntime {
public:
    struct Options {
        bool captureRecords = false;
        bool emitCanRxFrames = false;
        bool consumeTruth = true;
    };

    struct Result {
        bool capabilityFirstSeen = false;
        qint64 capabilityElapsedMs = -1;
        quint64 capabilityBytes = 0;
        QStringList errors;

        AnalysisFrameBatch analysisFrames;
        RawLedgerFrameBatch rawLedgerFrames;

        bool typedStatusDue = false;
        TypedIngressRuntime::StatusSnapshot typedStatus;

        bool projectionStatusDue = false;
        LiveProjectionRuntime::Status projectionStatus;

        bool truthStatusDue = false;
        LiveTruthRuntime::Status truthStatus;

        bool captureDrainNeeded = false;
        bool captureHandoffOverrun = false;
        QString captureHandoffError;
        quint64 captureHandoffOverrunRecords = 0;
        quint64 captureHandoffOverrunBytes = 0;

        QVector<CanMonitorCore::ViewChanged> viewChanges;
    };

    explicit CaptureCoreRuntime(QSharedPointer<TypedRecordHandoffQueue> captureQueue = {});

    void setCaptureQueue(QSharedPointer<TypedRecordHandoffQueue> queue);
    void setOptions(const Options& options);
    Options options() const { return m_options; }
    void reset();

    Result ingestBlocks(const QVector<DrainByteQueue::Block>& blocks,
                        qint64 handshakeElapsedMs,
                        quint64 parseBacklogBytes);
    TypedIngressRuntime::HandshakeWatchdogState evaluateHandshake(qint64 elapsedMs, qint64 timeoutMs) const;
    QJsonObject makeCaptureDiagnostics() const;
    TypedEvidencePipelineRuntime::Status status() const { return m_pipeline.status(); }
    LiveProjectionRuntime::Status projectionStatus() const { return m_liveProjection.status(); }
    CanMonitorCore::ViewQueryResult queryView(const CanMonitorCore::ViewQuery& query) const;
    QVector<CanMonitorCore::ViewChanged> viewChanges() const;

    LiveTruthRuntime::Status truthStatus() const { return m_liveTruth.status(); }

private:
    void appendCanRxFrames(const TypedRecord& record, QVector<CanRxLite>& out) const;
    void ingestRecordForViews(const TypedRecord& record, QVector<CanRxLite>& liveLatestFrames, Result& result);
    void pushCaptureBatch(TypedCaptureFrameList&& batch, Result& result);
    void ingestCriticalRecord(const TypedRecord& record, Result& result);
    void updateLiveLatestView(const QVector<CanRxLite>& frames, Result& result);
    void updateStatusViews(const Result& ingestResult, Result& out);
    static quint64 liveLatestKeyForFrame(const CanRxLite& frame);

    TypedEvidencePipelineRuntime m_pipeline;
    LiveProjectionRuntime m_liveProjection;
    LiveTruthRuntime m_liveTruth;
    CanMonitorCore::CoreMaterializedViewStore m_viewStore;
    QHash<quint64, CanRxLite> m_liveLatestByKey;
    QJsonObject m_coreEvidenceTransportPayload;
    QJsonObject m_coreEvidenceCheapCounts;
    CanMonitorCore::CoreViewSeverity m_coreEvidenceSeverity = CanMonitorCore::CoreViewSeverity::Ok;
    quint64 m_boardEventTotal = 0;
    quint64 m_mcp2515EventTotal = 0;
    quint64 m_boardEventFatalTotal = 0;
    QHash<quint16, quint64> m_mcp2515Details;
    quint64 m_liveLatestDropped = 0;
    QSharedPointer<TypedRecordHandoffQueue> m_captureQueue;
    Options m_options;
};

} // namespace CanMonitorTransport
