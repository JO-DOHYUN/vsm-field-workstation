#include "SerialWorker.h"

#include "AppLogging.h"

#include <QIODevice>
#include <QMetaObject>

#include <algorithm>

namespace {
constexpr int kTypedHandshakeWatchdogIntervalMs = 250;
constexpr int kTypedHandshakeTimeoutMs = 3500;
constexpr int kUiProjectionFlushIntervalMs = 55;
constexpr int kUiProjectionMaxFramesPerFlush = 72;
constexpr int kUiProjectionHardPendingKeys = 384;
}

SerialWorker::SerialWorker(QObject* parent)
    : QObject(parent),
      m_legacyIngress(this) {}

void SerialWorker::start(const QString& portName) {
    stop();

    m_typedIngress.resetStreamState();

    m_serial = new QSerialPort(this);
    m_serial->setPortName(portName);
    m_serial->setBaudRate(921600);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        qCWarning(logTransport).noquote() << "Serial open failed" << portName << m_serial->errorString();
        emit stateChanged(false, QStringLiteral("연결 실패: %1").arg(m_serial->errorString()));
        m_serial->deleteLater();
        m_serial = nullptr;
        return;
    }

    m_serial->setDataTerminalReady(true);
    m_serial->setRequestToSend(true);
    m_legacyIngress.resetStreamState();
    m_typedIngress.resetStreamState();
    resetProjectionQueue();
    connect(m_serial, &QSerialPort::readyRead, this, &SerialWorker::onReadyRead);
    connect(m_serial, &QSerialPort::bytesWritten, this, &SerialWorker::onBytesWritten);
    connect(m_serial, &QSerialPort::errorOccurred, this, [this, portName](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || !m_serial) return;

        const QString message = QStringLiteral("%1: %2").arg(portName, m_serial->errorString());
        qCWarning(logTransport).noquote() << "Serial runtime error" << int(error) << message;
        emit errorOccurred(QStringLiteral("Serial port error: %1").arg(message));

        if (error == QSerialPort::ResourceError ||
            error == QSerialPort::PermissionError ||
            error == QSerialPort::DeviceNotFoundError) {
            QMetaObject::invokeMethod(this, [this, message]() {
                closeSerialPortForRecovery(QStringLiteral("serial runtime error: %1").arg(message));
                emit stateChanged(false, QStringLiteral("Serial port closed after error"));
            }, Qt::QueuedConnection);
        }
    });
    if (m_transportMode == TransportMode::TypedEvidence) {
        startTypedHandshakeWatchdog();
    }
    const QString modeText = (m_transportMode == TransportMode::TypedEvidence)
        ? QStringLiteral("typed evidence")
        : QStringLiteral("legacy 20B");
    qCInfo(logTransport).noquote() << "Serial connected" << portName << modeText;
    emit stateChanged(true, QStringLiteral("연결됨: %1 · %2").arg(portName, modeText));
}

void SerialWorker::stop() {
    if (m_controlCycle.enabled() && m_serial && m_serial->isOpen()) {
        dispatchControlCycleResult(m_controlCycle.burstOnce(0,
                                                            0,
                                                            0.0,
                                                            1,
                                                            1,
                                                            m_controlCycle.bus(),
                                                            QStringLiteral("serial stop safety neutral"),
                                                            true,
                                                            false));
        drainHostTxQueue();
        m_serial->waitForBytesWritten(100);
    }
    stopControlCycle();
    stopTypedHandshakeWatchdog();
    emitLegacyLoggingUpdate(m_legacyIngress.stopLoggingIfActive());
    emitTypedStorageUpdate(m_typedIngress.finalizeStorageIfActive());
    closeSerialPortForRecovery(QStringLiteral("operator disconnect"));
    clearHostTxQueue(QStringLiteral("serial disconnected"));
    emit stateChanged(false, QStringLiteral("연결 해제"));
}

bool SerialWorker::setTypedStorage(bool enable, const QString& sessionDir, const QJsonObject& metadata) {
    if (enable) {
        if (m_transportMode != TransportMode::TypedEvidence) {
            emit errorOccurred(QStringLiteral("Typed storage requires TypedEvidence transport mode."));
            return false;
        }
        const auto update = m_typedIngress.startStorage(sessionDir, metadata);
        emitTypedStorageUpdate(update);
        return update.ok;
    }

    const auto update = m_typedIngress.stopStorage(sessionDir);
    emitTypedStorageUpdate(update);
    return update.ok;
}

void SerialWorker::sendHostFrame(const QByteArray& frame, const QString& summary) {
    if (frame.isEmpty()) {
        emit hostFrameWriteResult(false, QStringLiteral("empty host frame"), 0);
        return;
    }
    if (!m_serial || !m_serial->isOpen()) {
        emit hostFrameWriteResult(false, summary.isEmpty() ? QStringLiteral("serial not connected") : summary, 0);
        return;
    }
    if (!m_serial->isWritable()) {
        emit hostFrameWriteResult(false, summary.isEmpty() ? QStringLiteral("serial is not writable") : summary, 0);
        return;
    }

    const auto enqueue = m_hostTx.enqueue(frame, summary);
    if (!enqueue.ok) {
        emit hostFrameWriteResult(false, enqueue.error, 0);
        emitHostTxQueueStatus(enqueue.status);
        return;
    }

    emitHostTxQueueStatus(enqueue.status);
    drainHostTxQueue();
}

void SerialWorker::startControlCycle(int signedCommand,
                                     int rpm,
                                     double steeringDeg,
                                     quint8 motorMode,
                                     quint8 drivingMode,
                                     quint8 bus,
                                     int periodMs,
                                     int frameGapMs) {
    const int clampedPeriodMs = m_controlCycle.start(signedCommand,
                                                     rpm,
                                                     steeringDeg,
                                                     motorMode,
                                                     drivingMode,
                                                     bus,
                                                     periodMs,
                                                     frameGapMs);
    if (m_controlCycleTimerId != 0) killTimer(m_controlCycleTimerId);
    m_controlCycleTimerId = startTimer(clampedPeriodMs, Qt::PreciseTimer);
    beginControlCycle();
}

void SerialWorker::updateControlCycle(int signedCommand,
                                      int rpm,
                                      double steeringDeg,
                                      quint8 motorMode,
                                      quint8 drivingMode,
                                      quint8 bus) {
    m_controlCycle.update(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus);
}

void SerialWorker::stopControlCycle() {
    m_controlCycle.stop();
    if (m_controlCycleTimerId != 0) {
        killTimer(m_controlCycleTimerId);
        m_controlCycleTimerId = 0;
    }
    if (m_controlCycleGapTimerId != 0) {
        killTimer(m_controlCycleGapTimerId);
        m_controlCycleGapTimerId = 0;
    }
}

void SerialWorker::sendControlCycleBurstOnce(int signedCommand,
                                             int rpm,
                                             double steeringDeg,
                                             quint8 motorMode,
                                             quint8 drivingMode,
                                             quint8 bus,
                                             const QString& reason,
                                             bool resetSlew) {
    dispatchControlCycleResult(m_controlCycle.burstOnce(signedCommand,
                                                        rpm,
                                                        steeringDeg,
                                                        motorMode,
                                                        drivingMode,
                                                        bus,
                                                        reason,
                                                        resetSlew));
}

void SerialWorker::setLogging(bool enable, const QString& binPath, const QString& metaPath, const QString& rulesSnapshotPath, const QString& rulesSourcePath) {
    if (m_transportMode == TransportMode::TypedEvidence) {
        if (enable) {
            emit errorOccurred(QStringLiteral("Typed evidence mode uses typed capture storage; legacy 20B logging is disabled."));
        }
        return;
    }

    emitLegacyLoggingUpdate(enable
        ? m_legacyIngress.startLogging(binPath, metaPath, rulesSnapshotPath, rulesSourcePath)
        : m_legacyIngress.stopLogging());
}

void SerialWorker::onReadyRead() {
    if (!m_serial) return;
    processIncomingBytes(m_serial->readAll());
}

void SerialWorker::onBytesWritten(qint64 bytes) {
    Q_UNUSED(bytes);
    drainHostTxQueue();
}

void SerialWorker::timerEvent(QTimerEvent* event) {
    if (event->timerId() == m_typedHandshakeTimerId) {
        const qint64 elapsedMs = m_typedHandshakeClock.isValid() ? m_typedHandshakeClock.elapsed() : -1;
        const auto handshake = m_typedIngress.evaluateHandshake(elapsedMs, kTypedHandshakeTimeoutMs);
        if (handshake.capabilitySeen) {
            stopTypedHandshakeWatchdog();
            return;
        }
        if (handshake.timedOut) {
            qCWarning(logTransport).noquote() << handshake.timeoutReason;
            emit errorOccurred(handshake.timeoutReason);
            closeSerialPortForRecovery(handshake.timeoutReason);
            clearHostTxQueue(handshake.timeoutReason);
            emit stateChanged(false, QStringLiteral("Typed evidence handshake timeout; port closed"));
            return;
        }
        return;
    }
    if (event->timerId() == m_projectionFlushTimerId) {
        flushQueuedProjectionFrames(false);
        if (m_pendingProjectionFramesByKey.isEmpty()) {
            killTimer(m_projectionFlushTimerId);
            m_projectionFlushTimerId = 0;
            m_projectionFlushClock.invalidate();
        }
        return;
    }
    if (event->timerId() == m_controlCycleTimerId) {
        beginControlCycle();
        return;
    }
    if (event->timerId() == m_controlCycleGapTimerId) {
        if (m_controlCycleGapTimerId != 0) {
            killTimer(m_controlCycleGapTimerId);
            m_controlCycleGapTimerId = 0;
        }
        continueControlCycleBurst();
        return;
    }
    QObject::timerEvent(event);
}

void SerialWorker::setTransportMode(TransportMode mode) {
    if (m_transportMode == mode) return;
    m_transportMode = mode;
    m_legacyIngress.resetStreamState();
    m_typedIngress.resetStreamState();
    if (m_serial && m_serial->isOpen()) {
        if (m_transportMode == TransportMode::TypedEvidence) {
            startTypedHandshakeWatchdog();
        } else {
            stopTypedHandshakeWatchdog();
        }
    }
}

void SerialWorker::ingestBytesForTest(const QByteArray& bytes) {
    processIncomingBytes(bytes);
}

void SerialWorker::processIncomingBytes(const QByteArray& bytes) {
    if (bytes.isEmpty()) return;
    if (m_transportMode == TransportMode::TypedEvidence) {
        processTypedBytes(bytes);
        return;
    }
    processLegacyBytes(bytes);
}

void SerialWorker::processLegacyBytes(const QByteArray& bytes) {
    const auto result = m_legacyIngress.ingest(bytes);
    for (const QString& error : result.errors) {
        emit errorOccurred(error);
    }
    for (const FrameRecordList& batch : result.frameBatches) {
        emit framesReceived(batch);
    }
    for (const StatsRecord& stats : result.stats) {
        emit statsReceived(stats);
    }
    if (result.loggingProgressDue) {
        emit loggingProgress(result.loggingBytesWritten, result.loggingFrameCount);
    }
}

void SerialWorker::processTypedBytes(const QByteArray& bytes) {
    const qint64 handshakeElapsedMs = m_typedHandshakeClock.isValid() ? m_typedHandshakeClock.elapsed() : -1;
    const auto result = m_typedIngress.ingest(bytes, handshakeElapsedMs);
    if (result.capabilityFirstSeen) {
        qCInfo(logTransport).noquote()
            << "Typed CAPABILITY received after"
            << result.capabilityElapsedMs
            << "ms"
            << "bytes" << result.capabilityBytes;
        stopTypedHandshakeWatchdog();
    }
    for (const QString& error : result.errors) {
        emit errorOccurred(error);
    }
    for (const TypedRecordList& batch : result.recordBatches) {
        const auto projection = m_liveProjection.ingest(batch);
        if (!projection.criticalRecords.isEmpty()) emit typedRecordsReceived(projection.criticalRecords);
        if (!projection.projectedFrames.isEmpty()) {
            queueProjectedFrames(projection.projectedFrames);
        }
        if (projection.statusDue) {
            emitProjectionStatus(projection.status);
        }
    }
    if (result.storageProgressDue) {
        emit typedStorageProgress(result.storageBytesWritten, result.storageRecordCount);
    }
    if (result.statusDue) emitTypedStatus(result.status);
}

void SerialWorker::emitTypedStatus(const CanMonitorTransport::TypedIngressRuntime::StatusSnapshot& status) {
    emit typedTransportStatusChanged(status.frames,
                                     status.bytesDropped,
                                     status.crcFailures,
                                     status.lengthFailures,
                                     status.versionWarnings,
                                     status.seqGaps);
}

quint64 SerialWorker::projectionKeyForFrame(const FrameRecord& frame) {
    quint64 key = (quint64(frame.bus) << 56);
    if (frame.ext) key |= (quint64(1) << 55);
    if (frame.rtr) key |= (quint64(1) << 54);
    key |= quint64(frame.canId & 0x1FFFFFFFU);
    return key;
}

void SerialWorker::queueProjectedFrames(const FrameRecordList& frames) {
    if (frames.isEmpty()) return;

    for (const FrameRecord& frame : frames) {
        const quint64 key = projectionKeyForFrame(frame);
        auto existing = m_pendingProjectionFramesByKey.find(key);
        if (existing != m_pendingProjectionFramesByKey.end()) {
            existing.value() = frame;
            ++m_projectionQueueSampledFrames;
            continue;
        }
        if (m_pendingProjectionFramesByKey.size() >= kUiProjectionHardPendingKeys) {
            ++m_projectionQueueDroppedFrames;
            continue;
        }
        m_pendingProjectionFramesByKey.insert(key, frame);
    }

    if (m_projectionFlushTimerId == 0) {
        m_projectionFlushTimerId = startTimer(kUiProjectionFlushIntervalMs, Qt::CoarseTimer);
    }
    if (!m_projectionFlushClock.isValid() ||
        m_projectionFlushClock.elapsed() >= kUiProjectionFlushIntervalMs) {
        flushQueuedProjectionFrames(false);
    }
}

void SerialWorker::flushQueuedProjectionFrames(bool force) {
    if (m_pendingProjectionFramesByKey.isEmpty()) return;
    if (!force &&
        m_projectionFlushClock.isValid() &&
        m_projectionFlushClock.elapsed() < kUiProjectionFlushIntervalMs) {
        return;
    }

    FrameRecordList frames;
    frames.reserve(m_pendingProjectionFramesByKey.size());
    for (auto it = m_pendingProjectionFramesByKey.cbegin(); it != m_pendingProjectionFramesByKey.cend(); ++it) {
        frames.push_back(it.value());
    }
    m_pendingProjectionFramesByKey.clear();

    std::sort(frames.begin(), frames.end(), [](const FrameRecord& a, const FrameRecord& b) {
        if (a.tExtUs != b.tExtUs) return a.tExtUs < b.tExtUs;
        if (a.bus != b.bus) return a.bus < b.bus;
        return a.canId < b.canId;
    });
    if (frames.size() > kUiProjectionMaxFramesPerFlush) {
        const int dropCount = frames.size() - kUiProjectionMaxFramesPerFlush;
        m_projectionQueueDroppedFrames += quint64(dropCount);
        frames.erase(frames.begin(), frames.begin() + dropCount);
    }

    if (!frames.isEmpty()) emit framesReceived(frames);
    m_projectionFlushClock.restart();
    emitProjectionStatus(m_liveProjection.status());
}

void SerialWorker::emitProjectionStatus(const CanMonitorTransport::LiveProjectionRuntime::Status& status) {
    m_lastProjectionStatus = status;
    emit typedProjectionStatusChanged(status.observedCanRxFrames,
                                      status.projectedCanRxFrames,
                                      status.sampledCanRxFrames + m_projectionQueueSampledFrames,
                                      status.workerDroppedCanRxFrames + m_projectionQueueDroppedFrames,
                                      status.observedBus0CanRxFrames,
                                      status.observedBus1CanRxFrames,
                                      status.observedControlEvidenceRecords,
                                      status.projectedControlEvidenceRecords,
                                      status.sampledControlEvidenceRecords);
}

void SerialWorker::resetProjectionQueue() {
    if (m_projectionFlushTimerId != 0) {
        killTimer(m_projectionFlushTimerId);
        m_projectionFlushTimerId = 0;
    }
    m_pendingProjectionFramesByKey.clear();
    m_projectionFlushClock.invalidate();
    m_projectionQueueSampledFrames = 0;
    m_projectionQueueDroppedFrames = 0;
    m_lastProjectionStatus = {};
}

void SerialWorker::emitLegacyLoggingUpdate(const CanMonitorTransport::LegacyIngressRuntime::LoggingUpdate& update) {
    if (!update.error.isEmpty()) {
        emit errorOccurred(update.error);
    }
    if (update.progressDue) {
        emit loggingProgress(update.bytesWritten, update.frameCount);
    }
    if (update.stateChanged) {
        emit loggingStateChanged(update.active, update.path);
    }
}

void SerialWorker::emitTypedStorageUpdate(const CanMonitorTransport::TypedIngressRuntime::StorageUpdate& update) {
    if (!update.ok) {
        emit errorOccurred(update.error);
    }
    if (update.progressDue) {
        emit typedStorageProgress(update.bytesWritten, update.recordCount);
    }
    if (update.stateChanged) {
        emit typedStorageStateChanged(update.active, update.path);
    }
}

void SerialWorker::startTypedHandshakeWatchdog() {
    stopTypedHandshakeWatchdog();
    m_typedIngress.resetStreamState();
    m_liveProjection.reset();
    resetProjectionQueue();
    m_typedHandshakeClock.restart();
    m_typedHandshakeTimerId = startTimer(kTypedHandshakeWatchdogIntervalMs, Qt::CoarseTimer);
}

void SerialWorker::stopTypedHandshakeWatchdog() {
    if (m_typedHandshakeTimerId != 0) {
        killTimer(m_typedHandshakeTimerId);
        m_typedHandshakeTimerId = 0;
    }
    m_typedHandshakeClock.invalidate();
}

void SerialWorker::closeSerialPortForRecovery(const QString& reason) {
    stopTypedHandshakeWatchdog();
    stopControlCycle();
    flushQueuedProjectionFrames(true);
    emitTypedStorageUpdate(m_typedIngress.finalizeStorageIfActive());
    if (!m_serial) return;

    const QString portName = m_serial->portName();
    qCInfo(logTransport).noquote() << "Closing serial port" << portName << reason;
    disconnect(m_serial, nullptr, this, nullptr);
    if (m_serial->isOpen()) {
        m_serial->clear(QSerialPort::AllDirections);
        m_serial->setRequestToSend(false);
        m_serial->setDataTerminalReady(false);
        m_serial->close();
    }
    m_serial->deleteLater();
    m_serial = nullptr;
    m_typedIngress.resetStreamState();
    m_liveProjection.reset();
    resetProjectionQueue();
}

void SerialWorker::drainHostTxQueue() {
    if (!m_serial || !m_serial->isOpen() || !m_serial->isWritable()) return;

    while (true) {
        const auto item = m_hostTx.takeNextForWrite(m_serial->bytesToWrite());
        if (!item) break;
        const qint64 written = m_serial->write(item->frame);

        if (written != item->frame.size()) {
            emit hostFrameWriteResult(false,
                                      written < 0 ? QStringLiteral("%1 write failed: %2").arg(item->summary, m_serial->errorString())
                                                  : QStringLiteral("%1 partial write: %2/%3 bytes").arg(item->summary).arg(written).arg(item->frame.size()),
                                      written > 0 ? quint64(written) : 0);
            emitHostTxQueueStatus(m_hostTx.status());
            return;
        }

        m_hostTx.markWritten();
        emit hostFrameWriteResult(true, item->summary, quint64(written));
    }

    emitHostTxQueueStatus(m_hostTx.status());
}

void SerialWorker::clearHostTxQueue(const QString& reason) {
    const auto result = m_hostTx.clear(reason);
    if (result.hadPending) emit errorOccurred(result.error);
    emitHostTxQueueStatus(result.status);
}

void SerialWorker::emitHostTxQueueStatus(const CanMonitorTransport::HostTxRuntime::Status& status) {
    emit hostTxQueueChanged(status.queuedFrames,
                            status.queuedBytes,
                            status.enqueuedFrames,
                            status.writtenFrames,
                            status.droppedFrames);
}

void SerialWorker::beginControlCycle() {
    dispatchControlCycleResult(m_controlCycle.beginCycle());
}

void SerialWorker::continueControlCycleBurst() {
    dispatchControlCycleResult(m_controlCycle.continuePacedBurst());
}

void SerialWorker::dispatchControlCycleResult(const CanMonitorControl::ControlCycleRuntime::CycleResult& result) {
    for (const QString& error : result.errors) {
        emit errorOccurred(error);
    }
    for (const auto& frame : result.frames) {
        sendHostFrame(frame.frame, frame.summary);
    }
    if (result.scheduleGap) {
        if (m_controlCycleGapTimerId != 0) killTimer(m_controlCycleGapTimerId);
        m_controlCycleGapTimerId = startTimer(result.gapMs, Qt::PreciseTimer);
    }
}
