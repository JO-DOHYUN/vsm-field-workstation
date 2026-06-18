#include "SerialWorker.h"

#include "AppLogging.h"
#include "perf/PerformanceProbeRuntime.h"

#include <QIODevice>
#include <QMetaObject>
#include <QUrl>

#include <algorithm>
#include <cstring>

namespace {
constexpr int kTypedHandshakeWatchdogIntervalMs = 250;
constexpr int kTypedHandshakeTimeoutMs = 3500;
constexpr int kAnalysisSnapshotIntervalMs = 200;
constexpr int kUiProjectionFlushIntervalMs = 250;
constexpr int kUiProjectionMaxFramesPerFlush = 4;
constexpr int kUiProjectionHardPendingKeys = 64;
constexpr int kRawLedgerFlushIntervalMs = 60;
constexpr int kRawLedgerMaxRecordsPerFlush = 512;
constexpr qint64 kSerialReadBufferBytes = 4 * 1024 * 1024;
constexpr int kReadyReadMaxDrainLoops = 16;

FrameRecord typedCanToFrameRecord(const TypedRecord& record, const TypedCanRawRecord& can) {
    FrameRecord frame;
    frame.tExtUs = can.monoUs;
    frame.canId = can.canId;
    frame.ext = can.extended;
    frame.rtr = can.rtr;
    frame.dlc = can.dlc;
    frame.bus = can.bus;
    frame.seq = quint8(record.header.seq & 0xFF);
    std::memcpy(frame.data, can.data, sizeof(frame.data));
    return frame;
}

FrameRecord typedSegmentEntryToFrameRecord(const TypedRecord& record, const TypedCanRxSegmentEntry& entry) {
    FrameRecord frame;
    frame.tExtUs = entry.monoUs;
    frame.canId = entry.canId;
    frame.ext = entry.extended;
    frame.rtr = entry.rtr;
    frame.dlc = entry.dlc;
    frame.bus = entry.bus;
    frame.seq = quint8(record.header.seq & 0xFF);
    frame.hasCaptureSeq = true;
    frame.captureSeq = entry.captureSeq;
    std::memcpy(frame.data, entry.data, sizeof(frame.data));
    return frame;
}

template <typename Fn>
void forEachCanRxFrame(const TypedRecord& record, Fn&& fn) {
    if (record.isType(TypedRecordType::CanRxRaw)) {
        const auto can = decodeTypedCanRaw(record);
        if (can && !can->txAudit) fn(typedCanToFrameRecord(record, *can));
        return;
    }
    if (!record.isType(TypedRecordType::CanRxSegment)) return;
    const auto header = decodeTypedCanRxSegmentHeader(record);
    if (!header) return;
    for (qsizetype index = 0; index < header->frameCount; ++index) {
        const auto entry = decodeTypedCanRxSegmentEntry(record, index);
        if (entry) fn(typedSegmentEntryToFrameRecord(record, *entry));
    }
}

QVariantList rowsToVariantList(const QVector<QVariantMap>& rows) {
    QVariantList out;
    out.reserve(rows.size());
    for (const QVariantMap& row : rows) out.push_back(row);
    return out;
}
}

SerialWorker::SerialWorker(QObject* parent)
    : QObject(parent),
      m_legacyIngress(this) {}

void SerialWorker::start(const QString& portName) {
    stop();

    m_typedIngress.resetStreamState();
    const QString endpoint = portName.trimmed();
    if (endpoint.startsWith(QStringLiteral("tcp://"), Qt::CaseInsensitive)) {
        startGatewayTcp(endpoint);
        return;
    }

    m_serial = new QSerialPort(this);
    m_serial->setPortName(endpoint);
    m_serial->setBaudRate(921600);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);
    m_serial->setReadBufferSize(kSerialReadBufferBytes);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        qCWarning(logTransport).noquote() << "Serial open failed" << endpoint << m_serial->errorString();
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
    connect(m_serial, &QSerialPort::errorOccurred, this, [this, endpoint](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || !m_serial) return;

        const QString message = QStringLiteral("%1: %2").arg(endpoint, m_serial->errorString());
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
    qCInfo(logTransport).noquote() << "Serial connected" << endpoint << modeText;
    emit stateChanged(true, QStringLiteral("연결됨: %1 · %2").arg(endpoint, modeText));
}

void SerialWorker::stop() {
    if (m_controlCycle.enabled() && activeDeviceIsOpen()) {
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
        if (QIODevice* device = activeDevice()) device->waitForBytesWritten(100);
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
    if (!activeDeviceIsOpen()) {
        emit hostFrameWriteResult(false, summary.isEmpty() ? QStringLiteral("transport not connected") : summary, 0);
        return;
    }
    if (!activeDeviceIsWritable()) {
        emit hostFrameWriteResult(false, summary.isEmpty() ? QStringLiteral("transport is not writable") : summary, 0);
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
    QIODevice* device = activeDevice();
    if (!device) return;
    QByteArray bytes;
    const qint64 initialAvailable = std::max<qint64>(0, device->bytesAvailable());
    bytes.reserve(int(std::min<qint64>(initialAvailable, kSerialReadBufferBytes)));
    for (int pass = 0; pass < kReadyReadMaxDrainLoops; ++pass) {
        const QByteArray chunk = device->readAll();
        if (chunk.isEmpty()) break;
        bytes.append(chunk);
        if (device->bytesAvailable() <= 0) break;
    }
    if (!bytes.isEmpty()) processIncomingBytes(bytes);
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
    if (event->timerId() == m_rawLedgerFlushTimerId) {
        flushQueuedRawLedgerRecords(false);
        if (m_pendingRawLedgerRecords.isEmpty()) {
            killTimer(m_rawLedgerFlushTimerId);
            m_rawLedgerFlushTimerId = 0;
        }
        return;
    }
    if (event->timerId() == m_truthFlushTimerId) {
        flushQueuedTruthFrames(false);
        if (!m_liveTruth.hasPending()) {
            killTimer(m_truthFlushTimerId);
            m_truthFlushTimerId = 0;
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
    if (activeDeviceIsOpen()) {
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
    CanMonitorPerf::ScopedProbe probe("serial.process_incoming_bytes", activeBytesToWrite(), 2000);
    if (m_transportMode == TransportMode::TypedEvidence) {
        processTypedBytes(bytes);
        return;
    }
    processLegacyBytes(bytes);
}

void SerialWorker::processLegacyBytes(const QByteArray& bytes) {
    CanMonitorPerf::ScopedProbe probe("serial.legacy_ingest", bytes.size(), 2000);
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
    CanMonitorPerf::ScopedProbe probe("serial.typed_ingest", bytes.size(), 2000);
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
        queueAnalysisRecords(batch);
        queueRawLedgerRecords(batch);
        queueTruthFrames(batch);
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

void SerialWorker::setAnalysisConfig(const CanMonitorAnalysis::AnalysisRuntime::Config& config) {
    m_analysisRuntime.reset();
    m_analysisRuntime.setConfig(config);
    m_analysisRuntimeLatestUs = 0;
    emitAnalysisSnapshot(true);
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
    CanMonitorPerf::ScopedProbe probe("projection.queue_frames", m_pendingProjectionFramesByKey.size(), 1000);

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
    CanMonitorPerf::ScopedProbe probe("projection.flush_frames", m_pendingProjectionFramesByKey.size(), 2000);
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

void SerialWorker::queueRawLedgerRecords(const TypedRecordList& records) {
    if (records.isEmpty()) return;
    CanMonitorPerf::ScopedProbe probe("ledger.queue_records", records.size(), 2000);

    for (const TypedRecord& record : records) {
        if (record.isType(TypedRecordType::CanRxRaw) || record.isType(TypedRecordType::CanRxSegment)) {
            m_pendingRawLedgerRecords.push_back(record);
        }
    }
    if (m_pendingRawLedgerRecords.isEmpty()) return;
    if (m_rawLedgerFlushTimerId == 0) {
        m_rawLedgerFlushTimerId = startTimer(kRawLedgerFlushIntervalMs, Qt::CoarseTimer);
    }
    if (m_pendingRawLedgerRecords.size() >= kRawLedgerMaxRecordsPerFlush) {
        flushQueuedRawLedgerRecords(false);
    }
}

void SerialWorker::flushQueuedRawLedgerRecords(bool force) {
    Q_UNUSED(force);
    if (m_pendingRawLedgerRecords.isEmpty()) return;
    CanMonitorPerf::ScopedProbe probe("ledger.flush_records", m_pendingRawLedgerRecords.size(), 2000);
    TypedRecordList out;
    const int takeCount = std::min<int>(m_pendingRawLedgerRecords.size(), kRawLedgerMaxRecordsPerFlush);
    out.reserve(takeCount);
    for (int index = 0; index < takeCount; ++index) {
        out.push_back(m_pendingRawLedgerRecords.at(index));
    }
    m_pendingRawLedgerRecords.erase(m_pendingRawLedgerRecords.begin(), m_pendingRawLedgerRecords.begin() + takeCount);
    emit rawTypedRecordsReceived(out);
}

void SerialWorker::queueTruthFrames(const TypedRecordList& records) {
    CanMonitorPerf::ScopedProbe probe("truth.queue_records", records.size(), 2000);
    const auto result = m_liveTruth.ingest(records);
    if (!result.frames.isEmpty()) emit truthFramesReceived(result.frames);
    if (result.statusDue) emitTruthStatus(result.status);
    if (!m_liveTruth.hasPending()) return;
    if (m_truthFlushTimerId == 0) {
        m_truthFlushTimerId = startTimer(m_liveTruth.flushIntervalMs(), Qt::CoarseTimer);
    }
}

void SerialWorker::flushQueuedTruthFrames(bool force) {
    CanMonitorPerf::ScopedProbe probe("truth.flush_frames", m_liveTruth.status().pendingKeys, 2000);
    const FrameRecordList frames = m_liveTruth.flush(force);
    if (!frames.isEmpty()) emit truthFramesReceived(frames);
    emitTruthStatus(m_liveTruth.status());
}

void SerialWorker::queueAnalysisRecords(const TypedRecordList& records) {
    CanMonitorPerf::ScopedProbe probe("analysis.ingest_records", records.size(), 3000);
    bool accepted = false;
    for (const TypedRecord& record : records) {
        forEachCanRxFrame(record, [&](const FrameRecord& frame) {
            m_analysisRuntimeLatestUs = std::max(m_analysisRuntimeLatestUs, frame.tExtUs);
            m_analysisRuntime.ingestFrame(frame, QStringLiteral("live"));
            accepted = true;
        });
    }
    if (accepted) emitAnalysisSnapshot(false);
}

void SerialWorker::emitAnalysisSnapshot(bool force) {
    if (!force &&
        m_analysisSnapshotClock.isValid() &&
        m_analysisSnapshotClock.elapsed() < kAnalysisSnapshotIntervalMs) {
        return;
    }
    CanMonitorPerf::ScopedProbe probe("analysis.emit_snapshot", -1, 3000);
    const qint64 nowMs = qint64(m_analysisRuntimeLatestUs / 1000ULL);
    const auto snapshot = m_analysisRuntime.makeSnapshot(nowMs, QStringLiteral("live"));
    const QString level = snapshot.summary.value(QStringLiteral("level")).toString().isEmpty()
        ? QStringLiteral("OK")
        : snapshot.summary.value(QStringLiteral("level")).toString();
    emit analysisRuntimeSnapshotChanged(QStringLiteral("live"),
                                        level,
                                        snapshot.summary.value(QStringLiteral("text")).toString(),
                                        snapshot.diagnostics,
                                        rowsToVariantList(snapshot.timingRows),
                                        rowsToVariantList(snapshot.valueRows),
                                        rowsToVariantList(snapshot.alarmRows));
    m_analysisSnapshotClock.restart();
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

void SerialWorker::emitTruthStatus(const CanMonitorTransport::LiveTruthRuntime::Status& status) {
    emit typedTruthStatusChanged(status.observedCanRxFrames,
                                 status.emittedTruthFrames,
                                 status.coalescedTruthUpdates,
                                 status.observedBus0CanRxFrames,
                                 status.observedBus1CanRxFrames,
                                 status.flushCount,
                                 status.pendingKeys,
                                 status.maxPendingKeys,
                                 status.lastInputRecords,
                                 status.lastOutputFrames,
                                 status.lastFlushMs,
                                 status.truthLoss);
}

void SerialWorker::resetProjectionQueue() {
    if (m_projectionFlushTimerId != 0) {
        killTimer(m_projectionFlushTimerId);
        m_projectionFlushTimerId = 0;
    }
    if (m_truthFlushTimerId != 0) {
        killTimer(m_truthFlushTimerId);
        m_truthFlushTimerId = 0;
    }
    if (m_rawLedgerFlushTimerId != 0) {
        killTimer(m_rawLedgerFlushTimerId);
        m_rawLedgerFlushTimerId = 0;
    }
    m_pendingProjectionFramesByKey.clear();
    m_pendingRawLedgerRecords.clear();
    m_projectionFlushClock.invalidate();
    m_analysisSnapshotClock.invalidate();
    m_liveTruth.reset();
    m_analysisRuntime.reset();
    m_analysisRuntimeLatestUs = 0;
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
    flushQueuedTruthFrames(true);
    emitTypedStorageUpdate(m_typedIngress.finalizeStorageIfActive());
    if (!m_serial && !m_tcp) return;

    const QString transportName = activeTransportName();
    qCInfo(logTransport).noquote() << "Closing transport" << transportName << reason;
    if (m_serial) {
        disconnect(m_serial, nullptr, this, nullptr);
        if (m_serial->isOpen()) {
            m_serial->clear(QSerialPort::AllDirections);
            m_serial->setRequestToSend(false);
            m_serial->setDataTerminalReady(false);
            m_serial->close();
        }
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    if (m_tcp) {
        disconnect(m_tcp, nullptr, this, nullptr);
        if (m_tcp->isOpen()) {
            m_tcp->disconnectFromHost();
            if (m_tcp->state() != QAbstractSocket::UnconnectedState) {
                m_tcp->waitForDisconnected(100);
            }
            m_tcp->close();
        }
        m_tcp->deleteLater();
        m_tcp = nullptr;
    }
    m_typedIngress.resetStreamState();
    m_liveProjection.reset();
    resetProjectionQueue();
}

bool SerialWorker::startGatewayTcp(const QString& endpoint) {
    const QUrl url(endpoint);
    const QString host = url.host().isEmpty() ? QStringLiteral("127.0.0.1") : url.host();
    const quint16 port = quint16(url.port(0));
    if (port == 0) {
        emit stateChanged(false, QStringLiteral("gateway tcp endpoint requires a port: %1").arg(endpoint));
        return false;
    }

    m_tcp = new QTcpSocket(this);
    m_tcp->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_tcp, &QTcpSocket::readyRead, this, &SerialWorker::onReadyRead);
    connect(m_tcp, &QTcpSocket::bytesWritten, this, &SerialWorker::onBytesWritten);
    connect(m_tcp, &QTcpSocket::disconnected, this, [this]() {
        emit errorOccurred(QStringLiteral("Gateway TCP disconnected"));
        QMetaObject::invokeMethod(this, [this]() {
            closeSerialPortForRecovery(QStringLiteral("gateway tcp disconnected"));
            clearHostTxQueue(QStringLiteral("gateway tcp disconnected"));
            emit stateChanged(false, QStringLiteral("Gateway TCP disconnected"));
        }, Qt::QueuedConnection);
    });
    connect(m_tcp, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::UnknownSocketError || !m_tcp) return;
        const QString message = QStringLiteral("%1: %2").arg(activeTransportName(), m_tcp->errorString());
        qCWarning(logTransport).noquote() << "Gateway TCP runtime error" << int(error) << message;
        emit errorOccurred(QStringLiteral("Gateway TCP error: %1").arg(message));
    });

    m_legacyIngress.resetStreamState();
    m_typedIngress.resetStreamState();
    resetProjectionQueue();
    m_tcp->connectToHost(host, port);
    if (!m_tcp->waitForConnected(3500)) {
        const QString error = m_tcp->errorString();
        qCWarning(logTransport).noquote() << "Gateway TCP connect failed" << endpoint << error;
        emit stateChanged(false, QStringLiteral("Gateway TCP connect failed: %1").arg(error));
        m_tcp->deleteLater();
        m_tcp = nullptr;
        return false;
    }

    if (m_transportMode == TransportMode::TypedEvidence) {
        startTypedHandshakeWatchdog();
    }
    const QString modeText = (m_transportMode == TransportMode::TypedEvidence)
        ? QStringLiteral("typed evidence")
        : QStringLiteral("legacy 20B");
    qCInfo(logTransport).noquote() << "Gateway TCP connected" << endpoint << modeText;
    emit stateChanged(true, QStringLiteral("connected: %1 · %2").arg(endpoint, modeText));
    return true;
}

QIODevice* SerialWorker::activeDevice() const {
    if (m_serial) return m_serial;
    if (m_tcp) return m_tcp;
    return nullptr;
}

bool SerialWorker::activeDeviceIsOpen() const {
    const QIODevice* device = activeDevice();
    return device && device->isOpen();
}

bool SerialWorker::activeDeviceIsWritable() const {
    const QIODevice* device = activeDevice();
    return device && device->isOpen() && device->isWritable();
}

qint64 SerialWorker::activeBytesToWrite() const {
    if (m_serial) return m_serial->bytesToWrite();
    if (m_tcp) return m_tcp->bytesToWrite();
    return 0;
}

QString SerialWorker::activeTransportName() const {
    if (m_serial) return m_serial->portName();
    if (m_tcp) return QStringLiteral("tcp://%1:%2").arg(m_tcp->peerName()).arg(m_tcp->peerPort());
    return QStringLiteral("transport");
}

void SerialWorker::drainHostTxQueue() {
    QIODevice* device = activeDevice();
    if (!device || !device->isOpen() || !device->isWritable()) return;

    while (true) {
        const auto item = m_hostTx.takeNextForWrite(activeBytesToWrite());
        if (!item) break;
        const qint64 written = device->write(item->frame);

        if (written != item->frame.size()) {
            emit hostFrameWriteResult(false,
                                      written < 0 ? QStringLiteral("%1 write failed on %2").arg(item->summary, activeTransportName())
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
