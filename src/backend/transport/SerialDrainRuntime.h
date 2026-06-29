#pragma once

#include "transport/DrainByteQueue.h"
#include "transport/HostTxRuntime.h"
#include "transport/LivePathTelemetry.h"
#include "core/RuntimeProfile.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QSerialPort>
#include <QSharedPointer>
#include <QTcpSocket>

namespace CanMonitorTransport {

class SerialDrainRuntime : public QObject {
    Q_OBJECT
public:
    explicit SerialDrainRuntime(QSharedPointer<DrainByteQueue> queue,
                                CanMonitorCore::RuntimeTransportPolicy policy = CanMonitorCore::passiveProductProfile().transportPolicy(),
                                QObject* parent = nullptr);

public slots:
    void startSerial(const QString& portName);
    void startGatewayTcp(const QString& endpoint);
    void stop();
    void sendHostFrame(const QByteArray& frame, const QString& summary);
    void drainHostTxQueue();
    void acknowledgeBytesAvailable();

signals:
    void stateChanged(bool connected, const QString& message);
    void errorOccurred(const QString& message);
    void bytesAvailable();
    void hostFrameWriteResult(bool ok, const QString& summary, quint64 bytesWritten);
    void hostTxQueueChanged(quint64 queuedFrames, quint64 queuedBytes, quint64 enqueuedFrames, quint64 writtenFrames, quint64 droppedFrames);
    void drainStatusChanged(quint64 bytesTotal,
                            quint64 readyReadCount,
                            quint64 readyReadMaxUs,
                            quint64 drainBurstMaxBytes,
                            quint64 rawQueueUsedBytes,
                            quint64 rawQueueMaxUsedBytes,
                            quint64 rawQueueCapacityBytes,
                            quint64 rawQueueOverrunBytes,
                            quint64 rawQueueContentionCount);
    void drainEventTraceChanged(const QJsonObject& trace);

private slots:
    void onReadyRead();
    void onBytesWritten(qint64 bytes);

private:
    QIODevice* activeDevice() const;
    bool activeDeviceIsOpen() const;
    bool activeDeviceIsWritable() const;
    qint64 activeBytesToWrite() const;
    QString activeTransportName() const;
    void emitHostTxQueueStatus(const HostTxRuntime::Status& status);
    void clearHostTxQueue(const QString& reason);
    void emitDrainStatus(bool force = false);
    void emitBytesAvailableCoalesced();

    QSharedPointer<DrainByteQueue> m_queue;
    CanMonitorCore::RuntimeTransportPolicy m_policy;
    QSerialPort* m_serial = nullptr;
    QTcpSocket* m_tcp = nullptr;
    HostTxRuntime m_hostTx;
    QElapsedTimer m_statusClock;
    quint64 m_bytesTotal = 0;
    quint64 m_readyReadCount = 0;
    quint64 m_readyReadMaxUs = 0;
    quint64 m_drainBurstMaxBytes = 0;
    quint64 m_lastReportedOverrunBytes = 0;
    DrainEventTelemetry m_eventTelemetry;
    bool m_bytesAvailablePending = false;
};

} // namespace CanMonitorTransport
