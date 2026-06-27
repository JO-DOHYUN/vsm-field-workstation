#pragma once

#include "core/CoreIpcServerRuntime.h"
#include "core/CoreMaterializedViewStore.h"
#include "analysis/AnalysisWorkerRuntime.h"
#include "control/ControlCycleRuntime.h"
#include "transport/DrainByteQueue.h"
#include "transport/RawLedgerWriterRuntime.h"
#include "transport/SerialDrainRuntime.h"
#include "transport/TypedCaptureWriterWorkerRuntime.h"
#include "transport/TypedEvidencePipelineWorkerRuntime.h"
#include "transport/TypedRecordHandoffQueue.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSharedPointer>
#include <QThread>
#include <QTimerEvent>
#include <QVariantList>

namespace CanMonitorCore {

class CaptureCoreProcessRuntime : public QObject {
    Q_OBJECT
public:
    explicit CaptureCoreProcessRuntime(QObject* parent = nullptr);
    ~CaptureCoreProcessRuntime() override;

    bool startIpc(const QString& serverName, QString* errorOut = nullptr);
    void stopIpc();
    bool isIpcListening() const;
    QString serverName() const;

    void startSerial(const QString& portName);
    void startGatewayTcp(const QString& endpoint);
    void stopTransport();
    QJsonObject statusJson() const;

signals:
    void ipcReady(const QString& serverName);
    void transportStateChanged(bool connected, const QString& message);
    void errorOccurred(const QString& message);

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    void ensureTransportRuntime();
    void teardownTransportRuntime();
    void seedInitialViews();
    void updateCoreHealth(const QString& state, CoreViewSeverity severity = CoreViewSeverity::Ok);
    void updateTransportSummary(const QJsonObject& payload,
                                CoreViewSeverity severity = CoreViewSeverity::Ok,
                                const QJsonObject& cheapCounts = {});
    void updatePipelineTransportSummary(const QJsonObject& payload,
                                        CoreViewSeverity severity,
                                        const QJsonObject& cheapCounts);
    void ingestCriticalRecords(const TypedRecordList& records);
    void handleAnalysisModelRequested(quint64 requestId, const QString& modelPath, bool modelEnabled);
    void publishChange(const ViewChanged& change);
    void requestPipelineViewMirror(const QJsonObject& change);
    void applyPipelineSnapshot(quint64 requestId,
                               bool changed,
                               const QJsonObject& snapshot,
                               const QJsonObject& change);
    void handleHostFrameRequested(quint64 requestId, const QByteArray& frame, const QString& summary);
    void publishHostFrameWriteResult(bool ok, const QString& summary, quint64 bytesWritten);
    void handleControlCycleRequested(quint64 requestId, const QString& action, const QJsonObject& payload);
    void startControlCycle(const QJsonObject& payload);
    void updateControlCycle(const QJsonObject& payload);
    void stopControlCycle();
    void sendControlCycleBurstOnce(const QJsonObject& payload);
    void beginControlCycle();
    void continueControlCycleBurst();
    void dispatchControlCycleResult(const CanMonitorControl::ControlCycleRuntime::CycleResult& result);
    void sendCoreHostFrame(quint64 requestId, const QByteArray& frame, const QString& summary);
    void handleCaptureStartRequested(quint64 requestId, const QString& sessionDir, const QJsonObject& metadata);
    void handleCaptureStopRequested(quint64 requestId, const QString& inactivePath, const QJsonObject& diagnostics);
    void ensureCaptureWriterRuntime();
    void shutdownCaptureWriterRuntime();
    void ensureRawLedgerRuntime();
    void shutdownRawLedgerRuntime();
    void ensureAnalysisRuntime();
    void shutdownAnalysisRuntime();
    void queueAnalysisFrames(const FrameRecordList& frames);
    void publishAnalysisSnapshot(const QString& source,
                                 const QString& level,
                                 const QString& summary,
                                 const QVariantList& diagnostics,
                                 const QVariantList& timingRows,
                                 const QVariantList& valueRows,
                                 const QVariantList& alarmRows);
    void updateAnalysisStatus(quint64 queuedFrames,
                              quint64 maxQueuedFrames,
                              quint64 capacityFrames,
                              quint64 enqueuedFrames,
                              quint64 processedFrames,
                              quint64 overrunFrames,
                              quint64 pumpCount,
                              quint64 pumpMaxMs,
                              quint64 snapshotMaxMs,
                              quint64 truthLoss);
    void queueRawLedgerFrames(const FrameRecordList& frames);
    void flushRawLedgerFrames(bool force = false);
    void updateRawLedgerTailView(const FrameRecordList& frames,
                                 quint64 firstSeq,
                                 quint64 lastSeq,
                                 quint64 totalRows,
                                 quint64 segmentBytes,
                                 const QString& path);
    void updateRawLedgerStatusView(quint64 totalRows,
                                   quint64 segmentBytes,
                                   quint64 batchCount,
                                   quint64 writeMaxUs,
                                   quint64 writeFailures,
                                   const QString& lastError);
    void setPipelineCaptureEnabled(bool enabled, Qt::ConnectionType connectionType = Qt::QueuedConnection);
    void drainCaptureQueueSync();
    void publishCaptureStorageUpdate(quint64 requestId,
                                     const CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate& update);
    void updateCaptureProgressView(const CanMonitorTransport::TypedCaptureWriterRuntime::Status& status,
                                   const CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate* update = nullptr);

    CoreMaterializedViewStore m_viewStore;
    CoreIpcServerRuntime m_ipc;
    QSharedPointer<CanMonitorTransport::DrainByteQueue> m_drainQueue;
    QSharedPointer<CanMonitorTransport::TypedRecordHandoffQueue> m_captureQueue;
    QThread m_drainThread;
    QThread m_pipelineThread;
    QThread m_captureWriterThread;
    QThread m_rawLedgerThread;
    QThread m_analysisThread;
    QPointer<CanMonitorTransport::SerialDrainRuntime> m_drainRuntime;
    QPointer<CanMonitorTransport::TypedEvidencePipelineWorkerRuntime> m_pipelineRuntime;
    QPointer<CanMonitorTransport::TypedCaptureWriterWorkerRuntime> m_captureWriterRuntime;
    QPointer<CanMonitorTransport::RawLedgerWriterRuntime> m_rawLedgerRuntime;
    QPointer<CanMonitorAnalysis::AnalysisWorkerRuntime> m_analysisRuntime;
    FrameRecordList m_pendingRawLedgerFrames;
    QJsonArray m_rawLedgerTailRows;
    QHash<quint64, CoreViewName> m_pendingMirrorRequests;
    QQueue<quint64> m_pendingHostFrameRequests;
    CanMonitorControl::ControlCycleRuntime m_controlCycle;
    quint64 m_nextMirrorRequestId = 1;
    quint64 m_rawLedgerDroppedDisplayRows = 0;
    quint64 m_rawLedgerDroppedHandoffFrames = 0;
    quint64 m_rawLedgerLastTotalRows = 0;
    quint64 m_rawLedgerLastSegmentBytes = 0;
    quint64 m_rawLedgerLastBatchCount = 0;
    quint64 m_rawLedgerLastWriteMaxUs = 0;
    quint64 m_rawLedgerLastWriteFailures = 0;
    QJsonObject m_pipelineTransportPayload;
    QJsonObject m_pipelineTransportCheapCounts;
    QJsonObject m_coreEvidenceTransportPayload;
    QJsonObject m_coreEvidenceCheapCounts;
    QJsonObject m_analysisTransportPayload;
    QJsonObject m_analysisTransportCheapCounts;
    CoreViewSeverity m_pipelineTransportSeverity = CoreViewSeverity::Ok;
    CoreViewSeverity m_coreEvidenceSeverity = CoreViewSeverity::Ok;
    CoreViewSeverity m_analysisSeverity = CoreViewSeverity::Ok;
    quint64 m_boardEventTotal = 0;
    quint64 m_mcp2515EventTotal = 0;
    quint64 m_boardEventFatalTotal = 0;
    QHash<quint16, quint64> m_mcp2515Details;
    bool m_rawLedgerDispatchInFlight = false;
    bool m_transportRuntimeStarted = false;
    bool m_transportConnected = false;
    QString m_transportMessage = QStringLiteral("idle");
    int m_controlCycleTimerId = 0;
    int m_controlCycleGapTimerId = 0;
};

} // namespace CanMonitorCore
