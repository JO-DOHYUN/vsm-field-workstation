#pragma once

#include "CanTypes.h"
#include "control/ControlCycleRuntime.h"
#include "transport/HostTxRuntime.h"
#include "transport/LegacyIngressRuntime.h"
#include "transport/LiveProjectionRuntime.h"
#include "transport/TypedIngressRuntime.h"

#include <QJsonObject>
#include <QObject>
#include <QSerialPort>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimerEvent>
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

signals:
    void stateChanged(bool connected, const QString& message);
    void errorOccurred(const QString& message);
    void framesReceived(const FrameRecordList& frames);
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
    void typedTransportStatusChanged(quint64 frames, quint64 bytesDropped, quint64 crcFailures, quint64 lengthFailures, quint64 versionWarnings, quint64 seqGaps);
    void typedStorageStateChanged(bool active, const QString& path);
    void typedStorageProgress(quint64 bytesWritten, quint64 recordCount);
    void hostFrameWriteResult(bool ok, const QString& summary, quint64 bytesWritten);
    void loggingStateChanged(bool active, const QString& path);
    void loggingProgress(quint64 bytesWritten, quint64 frameCount);
    void hostTxQueueChanged(quint64 queuedFrames, quint64 queuedBytes, quint64 enqueuedFrames, quint64 writtenFrames, quint64 droppedFrames);

private slots:
    void onReadyRead();
    void onBytesWritten(qint64 bytes);

private:
    void timerEvent(QTimerEvent* event) override;
    void processIncomingBytes(const QByteArray& bytes);
    void processLegacyBytes(const QByteArray& bytes);
    void processTypedBytes(const QByteArray& bytes);
    void emitLegacyLoggingUpdate(const CanMonitorTransport::LegacyIngressRuntime::LoggingUpdate& update);
    void emitTypedStatus(const CanMonitorTransport::TypedIngressRuntime::StatusSnapshot& status);
    void emitTypedStorageUpdate(const CanMonitorTransport::TypedIngressRuntime::StorageUpdate& update);
    void emitHostTxQueueStatus(const CanMonitorTransport::HostTxRuntime::Status& status);
    void dispatchControlCycleResult(const CanMonitorControl::ControlCycleRuntime::CycleResult& result);
    void startTypedHandshakeWatchdog();
    void stopTypedHandshakeWatchdog();
    void closeSerialPortForRecovery(const QString& reason);
    void drainHostTxQueue();
    void clearHostTxQueue(const QString& reason);
    void beginControlCycle();
    void continueControlCycleBurst();

    QSerialPort* m_serial = nullptr;
    CanMonitorTransport::LegacyIngressRuntime m_legacyIngress;
    CanMonitorTransport::TypedIngressRuntime m_typedIngress;
    CanMonitorTransport::LiveProjectionRuntime m_liveProjection;
    CanMonitorTransport::HostTxRuntime m_hostTx;
    CanMonitorControl::ControlCycleRuntime m_controlCycle;
    QElapsedTimer m_typedHandshakeClock;
    int m_typedHandshakeTimerId = 0;
    int m_controlCycleTimerId = 0;
    int m_controlCycleGapTimerId = 0;
    TransportMode m_transportMode = TransportMode::Legacy20;
};
