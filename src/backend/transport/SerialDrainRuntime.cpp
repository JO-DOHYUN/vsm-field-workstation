#include "transport/SerialDrainRuntime.h"

#include <QDateTime>
#include <QLoggingCategory>

#include <algorithm>

Q_LOGGING_CATEGORY(logSerialDrain, "canmonitor.transport.drain")

namespace {
constexpr qint64 kSerialReadBufferBytes = 4 * 1024 * 1024;
constexpr qint64 kDrainReadChunkBytes = 64 * 1024;
constexpr int kReadyReadMaxDrainLoops = 1024;
constexpr int kDrainStatusIntervalMs = 250;
}

namespace CanMonitorTransport {

SerialDrainRuntime::SerialDrainRuntime(QSharedPointer<DrainByteQueue> queue,
                                       CanMonitorCore::RuntimeTransportPolicy policy,
                                       QObject* parent)
    : QObject(parent), m_queue(std::move(queue)), m_policy(policy) {}

void SerialDrainRuntime::startSerial(const QString& portName) {
    stop();
    const QString endpoint = portName.trimmed();
    m_queue->reset();
    m_bytesTotal = 0;
    m_readyReadCount = 0;
    m_readyReadMaxUs = 0;
    m_drainBurstMaxBytes = 0;
    m_lastReportedOverrunBytes = 0;
    m_bytesAvailablePending = false;
    m_eventTelemetry = DrainEventTelemetry{};
    m_statusClock.invalidate();

    m_serial = new QSerialPort(this);
    m_serial->setPortName(endpoint);
    m_serial->setBaudRate(2'000'000);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);
    m_serial->setReadBufferSize(kSerialReadBufferBytes);
    if (!m_serial->open(m_policy.serialOpenMode)) {
        const QString message = m_serial->errorString();
        qCWarning(logSerialDrain).noquote() << "Serial open failed" << endpoint << message;
        emit errorOccurred(QStringLiteral("Serial open failed: %1").arg(message));
        delete m_serial;
        m_serial = nullptr;
        emit stateChanged(false, QStringLiteral("Serial open failed"));
        return;
    }
    if (m_policy.touchDtr) {
        m_serial->setDataTerminalReady(m_policy.dtrAsserted);
    }
    if (m_policy.touchRts) {
        m_serial->setRequestToSend(m_policy.rtsAsserted);
    }
    connect(m_serial, &QSerialPort::readyRead, this, &SerialDrainRuntime::onReadyRead);
    connect(m_serial, &QSerialPort::bytesWritten, this, &SerialDrainRuntime::onBytesWritten);
    connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || !m_serial) return;
        const QString message = m_serial->errorString();
        emit errorOccurred(QStringLiteral("Serial port error: %1").arg(message));
        if (error == QSerialPort::ResourceError ||
            error == QSerialPort::PermissionError ||
            error == QSerialPort::DeviceNotFoundError) {
            stop();
            emit stateChanged(false, QStringLiteral("Serial port closed after error"));
        }
    });
    emit stateChanged(true,
                      QStringLiteral("Serial connected: %1 (%2)")
                          .arg(endpoint, m_policy.serialOpenModeText()));
    emitDrainStatus(true);
}

void SerialDrainRuntime::startGatewayTcp(const QString& endpoint) {
    stop();
    const QStringList parts = endpoint.mid(QStringLiteral("tcp://").size()).split(':');
    if (parts.size() != 2) {
        emit errorOccurred(QStringLiteral("Invalid gateway endpoint: %1").arg(endpoint));
        emit stateChanged(false, QStringLiteral("Gateway endpoint invalid"));
        return;
    }
    bool ok = false;
    const quint16 port = parts.at(1).toUShort(&ok);
    if (!ok) {
        emit errorOccurred(QStringLiteral("Invalid gateway port: %1").arg(endpoint));
        emit stateChanged(false, QStringLiteral("Gateway endpoint invalid"));
        return;
    }

    m_queue->reset();
    m_bytesTotal = 0;
    m_readyReadCount = 0;
    m_readyReadMaxUs = 0;
    m_drainBurstMaxBytes = 0;
    m_lastReportedOverrunBytes = 0;
    m_bytesAvailablePending = false;
    m_eventTelemetry = DrainEventTelemetry{};
    m_tcp = new QTcpSocket(this);
    connect(m_tcp, &QTcpSocket::readyRead, this, &SerialDrainRuntime::onReadyRead);
    connect(m_tcp, &QTcpSocket::bytesWritten, this, &SerialDrainRuntime::onBytesWritten);
    connect(m_tcp, &QTcpSocket::disconnected, this, [this]() {
        stop();
        emit stateChanged(false, QStringLiteral("Gateway TCP disconnected"));
    });
    connect(m_tcp, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_tcp) return;
        emit errorOccurred(QStringLiteral("Gateway TCP error: %1").arg(m_tcp->errorString()));
    });
    m_tcp->connectToHost(parts.at(0), port);
    if (!m_tcp->waitForConnected(1500)) {
        const QString message = m_tcp->errorString();
        delete m_tcp;
        m_tcp = nullptr;
        emit errorOccurred(QStringLiteral("Gateway TCP connect failed: %1").arg(message));
        emit stateChanged(false, QStringLiteral("Gateway TCP connect failed"));
        return;
    }
    emit stateChanged(true, QStringLiteral("Gateway TCP connected: %1").arg(endpoint));
    emitDrainStatus(true);
}

void SerialDrainRuntime::stop() {
    clearHostTxQueue(QStringLiteral("disconnect"));
    m_bytesAvailablePending = false;
    m_eventTelemetry.drainPumpScheduledFlag = false;
    if (m_serial) {
        if (m_serial->isOpen()) {
            if (m_policy.serialOpenMode.testFlag(QIODevice::WriteOnly)) {
                m_serial->clear(QSerialPort::AllDirections);
            }
            if (m_policy.touchRts) {
                m_serial->setRequestToSend(false);
            }
            if (m_policy.touchDtr) {
                m_serial->setDataTerminalReady(false);
            }
            m_serial->close();
        }
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    if (m_tcp) {
        m_tcp->disconnectFromHost();
        m_tcp->deleteLater();
        m_tcp = nullptr;
    }
    emitDrainStatus(true);
}

void SerialDrainRuntime::sendHostFrame(const QByteArray& frame, const QString& summary) {
    if (!m_policy.hostTxEnabled) {
        const QString message = QStringLiteral("Host TX disabled by runtime profile: %1").arg(summary);
        emit errorOccurred(message);
        emitHostTxQueueStatus(m_hostTx.status());
        emit hostFrameWriteResult(false, summary, 0);
        return;
    }
    const auto result = m_hostTx.enqueue(frame, summary);
    if (!result.ok) {
        emit errorOccurred(result.error);
        emitHostTxQueueStatus(result.status);
        emit hostFrameWriteResult(false, summary, 0);
        return;
    }
    emitHostTxQueueStatus(result.status);
    drainHostTxQueue();
}

void SerialDrainRuntime::acknowledgeBytesAvailable() {
    m_bytesAvailablePending = false;
    if (m_queue && m_queue->snapshot().usedBytes > 0) {
        emitBytesAvailableCoalesced();
    }
}

void SerialDrainRuntime::onReadyRead() {
    QIODevice* device = activeDevice();
    if (!device) return;

    QElapsedTimer timer;
    timer.start();
    quint64 burstBytes = 0;
    int readLoops = 0;
    ++m_readyReadCount;
    ++m_eventTelemetry.readyReadCalls;

    for (int pass = 0; pass < kReadyReadMaxDrainLoops; ++pass) {
        QByteArray chunk = device->read(kDrainReadChunkBytes);
        if (chunk.isEmpty()) break;
        ++readLoops;
        burstBytes += quint64(chunk.size());
        m_bytesTotal += quint64(chunk.size());
        if (!m_queue->push(std::move(chunk))) {
            const auto snapshot = m_queue->snapshot();
            if (snapshot.overrunBytes != m_lastReportedOverrunBytes) {
                m_lastReportedOverrunBytes = snapshot.overrunBytes;
                emit errorOccurred(QStringLiteral("Host drain raw queue overrun: %1 bytes").arg(snapshot.overrunBytes));
            }
            break;
        }
        if (device->bytesAvailable() <= 0) break;
    }

    m_drainBurstMaxBytes = std::max(m_drainBurstMaxBytes, burstBytes);
    m_eventTelemetry.readBurstBytesLast = burstBytes;
    m_eventTelemetry.readBurstBytesMax = std::max(m_eventTelemetry.readBurstBytesMax, burstBytes);
    m_eventTelemetry.readLoopsLast = quint64(readLoops);
    m_eventTelemetry.readLoopsMax = std::max<quint64>(m_eventTelemetry.readLoopsMax, quint64(readLoops));
    m_readyReadMaxUs = std::max<quint64>(m_readyReadMaxUs, quint64(timer.nsecsElapsed() / 1000));
    if (burstBytes > 0) emitBytesAvailableCoalesced();
    emitDrainStatus(false);
}

void SerialDrainRuntime::onBytesWritten(qint64 bytes) {
    Q_UNUSED(bytes);
    drainHostTxQueue();
}

void SerialDrainRuntime::drainHostTxQueue() {
    if (!m_policy.hostTxEnabled) {
        emitHostTxQueueStatus(m_hostTx.status());
        return;
    }
    if (!activeDeviceIsWritable()) {
        emitHostTxQueueStatus(m_hostTx.status());
        return;
    }

    while (activeDeviceIsWritable()) {
        const auto item = m_hostTx.takeNextForWrite(activeBytesToWrite());
        if (!item.has_value()) break;
        const qint64 written = activeDevice()->write(item->frame);
        if (written != item->frame.size()) {
            emit hostFrameWriteResult(false, item->summary, quint64(std::max<qint64>(0, written)));
            emit errorOccurred(QStringLiteral("%1 write failed on %2").arg(item->summary, activeTransportName()));
            break;
        }
        m_hostTx.markWritten();
        emit hostFrameWriteResult(true, item->summary, quint64(written));
    }
    emitHostTxQueueStatus(m_hostTx.status());
}

QIODevice* SerialDrainRuntime::activeDevice() const {
    if (m_tcp) return m_tcp;
    return m_serial;
}

bool SerialDrainRuntime::activeDeviceIsOpen() const {
    const QIODevice* device = activeDevice();
    return device && device->isOpen();
}

bool SerialDrainRuntime::activeDeviceIsWritable() const {
    const QIODevice* device = activeDevice();
    return device && device->isOpen() && device->isWritable();
}

qint64 SerialDrainRuntime::activeBytesToWrite() const {
    if (m_tcp) return m_tcp->bytesToWrite();
    if (m_serial) return m_serial->bytesToWrite();
    return 0;
}

QString SerialDrainRuntime::activeTransportName() const {
    if (m_tcp) return QStringLiteral("gateway TCP");
    return QStringLiteral("serial");
}

void SerialDrainRuntime::emitHostTxQueueStatus(const HostTxRuntime::Status& status) {
    emit hostTxQueueChanged(status.queuedFrames,
                            status.queuedBytes,
                            status.enqueuedFrames,
                            status.writtenFrames,
                            status.droppedFrames);
}

void SerialDrainRuntime::clearHostTxQueue(const QString& reason) {
    const auto cleared = m_hostTx.clear(reason);
    if (cleared.hadPending && !cleared.error.isEmpty()) emit errorOccurred(cleared.error);
    emitHostTxQueueStatus(cleared.status);
}

void SerialDrainRuntime::emitDrainStatus(bool force) {
    if (!force && m_statusClock.isValid() && m_statusClock.elapsed() < kDrainStatusIntervalMs) return;
    m_statusClock.restart();
    const auto snapshot = m_queue->snapshot();
    m_eventTelemetry.drainQueueUsedBytes = snapshot.usedBytes;
    m_eventTelemetry.drainQueueMaxUsedBytes = snapshot.maxUsedBytes;
    m_eventTelemetry.drainQueueCapacityBytes = snapshot.capacityBytes;
    m_eventTelemetry.drainQueueOverrunBytes = snapshot.overrunBytes;
    emit drainStatusChanged(m_bytesTotal,
                            m_readyReadCount,
                            m_readyReadMaxUs,
                            m_drainBurstMaxBytes,
                            snapshot.usedBytes,
                            snapshot.maxUsedBytes,
                            snapshot.capacityBytes,
                            snapshot.overrunBytes,
                            snapshot.contentionCount);
    QJsonObject trace = m_eventTelemetry.toJson(QDateTime::currentMSecsSinceEpoch());
    insertCounter(trace, QStringLiteral("drain_bytes_total"), m_bytesTotal);
    insertCounter(trace, QStringLiteral("ready_read_count"), m_readyReadCount);
    insertCounter(trace, QStringLiteral("ready_read_max_us"), m_readyReadMaxUs);
    insertCounter(trace, QStringLiteral("drain_burst_max_bytes"), m_drainBurstMaxBytes);
    insertCounter(trace, QStringLiteral("drain_queue_contention_count"), snapshot.contentionCount);
    emit drainEventTraceChanged(trace);
}

void SerialDrainRuntime::emitBytesAvailableCoalesced() {
    if (m_bytesAvailablePending) return;
    m_bytesAvailablePending = true;
    ++m_eventTelemetry.bytesAvailableEmits;
    m_eventTelemetry.drainPumpScheduledFlag = true;
    emit bytesAvailable();
}

} // namespace CanMonitorTransport
