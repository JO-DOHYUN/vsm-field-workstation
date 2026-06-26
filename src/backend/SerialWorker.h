#pragma once

#include "CanTypes.h"
#include "analysis/AnalysisWorkerRuntime.h"
#include "control/ControlCycleRuntime.h"
#include "transport/DrainByteQueue.h"
#include "transport/HostTxRuntime.h"
#include "transport/LegacyIngressRuntime.h"
#include "transport/LivePathTelemetry.h"
#include "transport/LiveProjectionRuntime.h"
#include "transport/LiveTruthRuntime.h"
#include "transport/RawLedgerWriterRuntime.h"
#include "transport/SerialDrainRuntime.h"
#include "transport/TypedCaptureWriterRuntime.h"
#include "transport/TypedCaptureWriterWorkerRuntime.h"
#include "transport/TypedEvidencePipelineRuntime.h"
#include "transport/TypedEvidencePipelineWorkerRuntime.h"
#include "transport/TypedIngressRuntime.h"
#include "transport/TypedRecordHandoffQueue.h"

#include <QJsonObject>
#include <QIODevice>
#include <QObject>
#include <QSerialPort>
#include <QSharedPointer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QHash>
#include <QStringList>
#include <QThread>
#include <QTimerEvent>
#include <QVariantList>
#include <QVector>

class SerialWorker : public QObject {
    Q_OBJECT
public:
    enum class TransportMode {
        Legacy20 = 0,
        TypedEvidence = 1
    };
    Q_ENUM(TransportMode)

    explicit SerialWorker(QObject* parent = nullptr);
    ~SerialWorker() override;
    TransportMode transportMode() const { return m_transportMode; }
    void ingestBytesForTest(const QByteArray& bytes);

public slots:
    void start(const QString& portName);
    void stop();
    void setTransportMode(TransportMode mode);
    void setLogging(bool enable, const QString& binPath, const QString& metaPath, const QString& rulesSnapshotPath, const QString& rulesSourcePath);
    bool setTypedStorage(bool enable, const QString& sessionDir, const QJsonObject& metadata);
    void sendHostFrame(const QByteArray& frame, const QString& summary);
    void startControlCycle(int signedCommand, int rpm, double steeringDeg, quint8 motorMode, quint8 drivingMode, quint8 bus, int periodMs, int frameGapMs);
    void updateControlCycle(int signedCommand, int rpm, double steeringDeg, quint8 motorMode, quint8 drivingMode, quint8 bus);
    void stopControlCycle();
    void sendControlCycleBurstOnce(int signedCommand, int rpm, double steeringDeg, quint8 motorMode, quint8 drivingMode, quint8 bus, const QString& reason, bool resetSlew = false);
    void setAnalysisConfig(const CanMonitorAnalysis::AnalysisRuntime::Config& config);
    void resetRawLedger(const QString& label = QStringLiteral("live"));
    void requestCoreView(const QString& viewName, quint64 sinceSeq, int limit, quint64 requestId);

signals:
    void stateChanged(bool connected, const QString& message);
    void errorOccurred(const QString& message);
    void framesReceived(const FrameRecordList& frames);
    void rawFramesReceived(const FrameRecordList& frames);
    void rawTypedRecordsReceived(const TypedRecordList& records);
    void rawLedgerReset(bool ok, const QString& path, const QString& error);
    void rawLedgerBatchCommitted(const FrameRecordList& frames,
                                 quint64 firstSeq,
                                 quint64 lastSeq,
                                 quint64 totalRows,
                                 quint64 segmentBytes,
                                 const QString& path);
    void rawLedgerWriterStatusChanged(quint64 queueBytes,
                                      quint64 maxQueueBytes,
                                      quint64 overrunBytes,
                                      quint64 writeMaxUs,
                                      quint64 writeFailures,
                                      const QString& lastError);
    void truthFramesReceived(const FrameRecordList& frames);
    void statsReceived(const StatsRecord& st);
    void typedRecordsReceived(const TypedRecordList& records);
    void typedProjectionStatusChanged(quint64 observedCanRxFrames,
                                      quint64 projectedCanRxFrames,
                                      quint64 sampledCanRxFrames,
                                      quint64 workerDroppedCanRxFrames,
                                      quint64 observedBus0CanRxFrames,
                                      quint64 observedBus1CanRxFrames,
                                      quint64 observedControlEvidenceRecords,
                                      quint64 projectedControlEvidenceRecords,
                                      quint64 sampledControlEvidenceRecords);
    void typedTruthStatusChanged(quint64 observedCanRxFrames,
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
    void typedTransportStatusChanged(quint64 frames, quint64 bytesDropped, quint64 crcFailures, quint64 lengthFailures, quint64 versionWarnings, quint64 seqGaps);
    void analysisRuntimeSnapshotChanged(const QString& source,
                                        const QString& level,
                                        const QString& summary,
                                        const QVariantList& diagnostics,
                                        const QVariantList& timingRows,
                                        const QVariantList& valueRows,
                                        const QVariantList& alarmRows);
    void typedStorageStateChanged(bool active, const QString& path);
    void typedStorageProgress(quint64 bytesWritten, quint64 recordCount);
    void hostFrameWriteResult(bool ok, const QString& summary, quint64 bytesWritten);
    void loggingStateChanged(bool active, const QString& path);
    void loggingProgress(quint64 bytesWritten, quint64 frameCount);
    void hostTxQueueChanged(quint64 queuedFrames, quint64 queuedBytes, quint64 enqueuedFrames, quint64 writtenFrames, quint64 droppedFrames);
    void drainStatusChanged(quint64 bytesTotal,
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
    void livePathTraceChanged(const QJsonObject& trace);
    void drainEventTraceChanged(const QJsonObject& trace);
    void coreViewChanged(const QJsonObject& change);
    void coreViewSnapshotReady(quint64 requestId, bool changed, const QJsonObject& snapshot, const QJsonObject& change);
    void analysisQueueStatusChanged(quint64 queuedFrames,
                                    quint64 maxQueuedFrames,
                                    quint64 capacityFrames,
                                    quint64 enqueuedFrames,
                                    quint64 processedFrames,
                                    quint64 overrunFrames,
                                    quint64 pumpCount,
                                    quint64 pumpMaxMs,
                                    quint64 snapshotMaxMs,
                                    quint64 truthLoss);

private slots:
    void onReadyRead();
    void onBytesWritten(qint64 bytes);
    void scheduleDrainPump();
    void processDrainQueue();

private:
    void timerEvent(QTimerEvent* event) override;
    void processIncomingBytes(const QByteArray& bytes);
    void processLegacyBytes(const QByteArray& bytes);
    void processTypedBytes(const QByteArray& bytes);
    void emitLegacyLoggingUpdate(const CanMonitorTransport::LegacyIngressRuntime::LoggingUpdate& update);
    void emitTypedStatus(const CanMonitorTransport::TypedIngressRuntime::StatusSnapshot& status);
    void emitTypedStorageUpdate(const CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate& update);
    void emitHostTxQueueStatus(const CanMonitorTransport::HostTxRuntime::Status& status);
    void queueProjectedFrames(const FrameRecordList& frames);
    void flushQueuedProjectionFrames(bool force = false);
    void queueRawLedgerFrames(const FrameRecordList& frames);
    void flushQueuedRawLedgerRecords(bool force = false);
    void flushRawLedgerHandoffSync();
    void queueTruthFrames(const TypedRecordList& records);
    void flushQueuedTruthFrames(bool force = false);
    void queueAnalysisFrames(const FrameRecordList& frames);
    void scheduleAnalysisDispatch();
    void dispatchAnalysisFrames();
    void queueCaptureWriterRecords(const TypedRecordList& records);
    void scheduleCaptureWriterDispatch();
    void dispatchCaptureWriterRecords();
    void flushCaptureWriterHandoffSync();
    void emitProjectionStatus(const CanMonitorTransport::LiveProjectionRuntime::Status& status);
    void emitTruthStatus(const CanMonitorTransport::LiveTruthRuntime::Status& status);
    void resetProjectionQueue();
    static quint64 projectionKeyForFrame(const FrameRecord& frame);
    bool startGatewayTcp(const QString& endpoint);
    void ensureDrainRuntime();
    void shutdownDrainRuntime();
    void ensureTypedPipelineRuntime();
    void shutdownTypedPipelineRuntime();
    void handleTypedRecordBatch(const TypedRecordList& batch);
    void ensureAnalysisRuntime();
    void shutdownAnalysisRuntime();
    void resetAnalysisWorker();
    void ensureCaptureWriterRuntime();
    void shutdownCaptureWriterRuntime();
    void ensureRawLedgerRuntime();
    void shutdownRawLedgerRuntime();
    CanMonitorTransport::TypedCaptureWriterRuntime::StorageUpdate finalizeCaptureWriterIfActive();
    void emitDrainPipelineStatus(bool force = false);
    QIODevice* activeDevice() const;
    bool activeDeviceIsOpen() const;
    bool activeDeviceIsWritable() const;
    qint64 activeBytesToWrite() const;
    QString activeTransportName() const;
    void dispatchControlCycleResult(const CanMonitorControl::ControlCycleRuntime::CycleResult& result);
    void startTypedHandshakeWatchdog();
    void stopTypedHandshakeWatchdog();
    void closeSerialPortForRecovery(const QString& reason);
    void drainHostTxQueue();
    void clearHostTxQueue(const QString& reason);
    void beginControlCycle();
    void continueControlCycleBurst();

    QSerialPort* m_serial = nullptr;
    QTcpSocket* m_tcp = nullptr;
    CanMonitorTransport::LegacyIngressRuntime m_legacyIngress;
    CanMonitorTransport::TypedEvidencePipelineRuntime m_typedPipeline;
    CanMonitorTransport::LiveProjectionRuntime m_liveProjection;
    CanMonitorTransport::LiveTruthRuntime m_liveTruth;
    CanMonitorTransport::HostTxRuntime m_hostTx;
    CanMonitorControl::ControlCycleRuntime m_controlCycle;
    QThread m_drainThread;
    QSharedPointer<CanMonitorTransport::DrainByteQueue> m_drainQueue;
    QSharedPointer<CanMonitorTransport::TypedRecordHandoffQueue> m_captureRecordQueue;
    CanMonitorTransport::SerialDrainRuntime* m_drainRuntime = nullptr;
    QThread m_typedPipelineThread;
    CanMonitorTransport::TypedEvidencePipelineWorkerRuntime* m_typedPipelineWorker = nullptr;
    QThread m_analysisThread;
    CanMonitorAnalysis::AnalysisWorkerRuntime* m_analysisWorker = nullptr;
    QThread m_captureWriterThread;
    CanMonitorTransport::TypedCaptureWriterWorkerRuntime* m_captureWriterWorker = nullptr;
    QThread m_rawLedgerThread;
    CanMonitorTransport::RawLedgerWriterRuntime* m_rawLedgerWorker = nullptr;
    QHash<quint64, FrameRecord> m_pendingProjectionFramesByKey;
    FrameRecordList m_pendingRawLedgerFrames;
    TypedRecordList m_pendingCaptureWriterRecords;
    FrameRecordList m_pendingAnalysisFrames;
    QElapsedTimer m_projectionFlushClock;
    QElapsedTimer m_drainStatusClock;
    int m_projectionFlushTimerId = 0;
    int m_rawLedgerFlushTimerId = 0;
    int m_truthFlushTimerId = 0;
    quint64 m_projectionQueueSampledFrames = 0;
    quint64 m_projectionQueueDroppedFrames = 0;
    CanMonitorTransport::LiveProjectionRuntime::Status m_lastProjectionStatus;
    CanMonitorTransport::TypedEvidencePipelineRuntime::Status m_pipelineStatus;
    QJsonObject m_pipelineCaptureDiagnostics;
    quint64 m_drainBytesTotal = 0;
    quint64 m_drainReadyReadCount = 0;
    quint64 m_drainReadyReadMaxUs = 0;
    quint64 m_drainBurstMaxBytes = 0;
    quint64 m_drainRawQueueUsedBytes = 0;
    quint64 m_drainRawQueueMaxUsedBytes = 0;
    quint64 m_drainRawQueueCapacityBytes = 0;
    quint64 m_drainRawQueueOverrunBytes = 0;
    quint64 m_drainRawQueueContentionCount = 0;
    quint64 m_captureWriterQueueBytes = 0;
    quint64 m_captureWriterMaxQueueBytes = 0;
    quint64 m_captureWriterOverrunBytes = 0;
    quint64 m_captureWriteMaxMs = 0;
    quint64 m_pendingRawLedgerBytes = 0;
    quint64 m_pendingRawLedgerMaxBytes = 0;
    quint64 m_rawLedgerHandoffOverrunBytes = 0;
    quint64 m_rawLedgerWriteMaxUs = 0;
    quint64 m_rawLedgerWriteFailures = 0;
    QString m_rawLedgerLastError;
    quint64 m_pendingCaptureWriterBytes = 0;
    quint64 m_pendingCaptureWriterMaxBytes = 0;
    quint64 m_captureWriterHandoffOverrunBytes = 0;
    quint64 m_analysisHandoffOverrunFrames = 0;
    quint64 m_rawLedgerDispatchInFlightFrames = 0;
    quint64 m_analysisDispatchInFlightFrames = 0;
    quint64 m_captureWriterDispatchInFlightRecords = 0;
    CanMonitorTransport::LivePathTelemetry m_livePathTelemetry;
    CanMonitorTransport::DrainEventTelemetry m_drainEventTelemetry;
    QJsonObject m_drainRuntimeEventTrace;
    QJsonObject m_pipelineLivePathTrace;
    QJsonObject m_pipelineDrainEventTrace;
    bool m_drainPumpScheduled = false;
    bool m_captureWriterDispatchScheduled = false;
    bool m_captureWriterDispatchInFlight = false;
    bool m_rawLedgerDispatchInFlight = false;
    bool m_analysisDispatchScheduled = false;
    bool m_analysisDispatchInFlight = false;
    bool m_typedCaptureEnabled = false;
    bool m_connected = false;
    QElapsedTimer m_typedHandshakeClock;
    int m_typedHandshakeTimerId = 0;
    int m_controlCycleTimerId = 0;
    int m_controlCycleGapTimerId = 0;
    TransportMode m_transportMode = TransportMode::Legacy20;
};
