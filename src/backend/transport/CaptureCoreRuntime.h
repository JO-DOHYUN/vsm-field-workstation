#pragma once

#include "transport/LiveProjectionRuntime.h"
#include "transport/LiveTruthRuntime.h"
#include "transport/TypedEvidencePipelineRuntime.h"
#include "transport/TypedRecordHandoffQueue.h"

#include <QSharedPointer>

namespace CanMonitorTransport {

class CaptureCoreRuntime {
public:
    struct Options {
        bool captureRecords = false;
        bool emitCanRxFrames = false;
        bool emitProjectionFrames = true;
        bool emitTruthFrames = false;
    };

    struct Result {
        bool capabilityFirstSeen = false;
        qint64 capabilityElapsedMs = -1;
        quint64 capabilityBytes = 0;
        QStringList errors;

        FrameRecordList canRxFrames;
        FrameRecordList projectedFrames;
        FrameRecordList truthFrames;
        TypedRecordList criticalRecords;

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

    FrameRecordList flushTruth(bool force);
    LiveTruthRuntime::Status truthStatus() const { return m_liveTruth.status(); }

private:
    void appendCanRxFrames(const TypedRecord& record, FrameRecordList& out) const;
    void ingestBatch(TypedRecordList&& batch, Result& result);

    TypedEvidencePipelineRuntime m_pipeline;
    LiveProjectionRuntime m_liveProjection;
    LiveTruthRuntime m_liveTruth;
    QSharedPointer<TypedRecordHandoffQueue> m_captureQueue;
    Options m_options;
};

} // namespace CanMonitorTransport
