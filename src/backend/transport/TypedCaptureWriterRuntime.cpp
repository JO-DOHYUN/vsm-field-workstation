#include "transport/TypedCaptureWriterRuntime.h"

#include <QJsonObject>

#include <algorithm>

namespace {
constexpr quint64 kWriterQueueMaxBytes = 16ULL * 1024ULL * 1024ULL;
constexpr int kWriterFlushMaxRecords = 4096;
constexpr quint64 kTypedStorageProgressRecordStep = 1536;
constexpr int kTypedStorageProgressIntervalMs = 1000;
}

namespace CanMonitorTransport {

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterRuntime::startStorage(const QString& sessionDir,
                                                                                 const QJsonObject& metadata) {
    if (m_storage.isActive()) {
        StorageUpdate update;
        update.ok = false;
        update.error = QStringLiteral("Typed storage is already active.");
        return update;
    }

    resetQueue();
    QString error;
    if (!m_storage.startTypedSession(sessionDir, metadata, &error)) {
        StorageUpdate update;
        update.ok = false;
        update.error = QStringLiteral("Typed storage start failed: %1").arg(error);
        return update;
    }

    m_lastReportedStorageRecordCount = 0;
    m_storageProgressTimer.restart();
    return makeStorageUpdate(true, true, m_storage.paths().sessionDir, true);
}

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterRuntime::stopStorage(const QString& inactivePath,
                                                                                const QJsonObject& diagnostics) {
    if (!m_storage.isActive()) {
        return makeStorageUpdate(true, false, inactivePath, false);
    }
    return finalizeStorageIfActive(diagnostics);
}

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterRuntime::finalizeStorageIfActive(const QJsonObject& diagnostics) {
    if (!m_storage.isActive()) return {};

    StorageUpdate flush = flushQueued(true);
    const QString path = m_storage.paths().sessionDir;
    QString error;
    const quint64 bytesWritten = m_storage.bytesWritten();
    const quint64 recordCount = m_storage.recordCount();
    const bool ok = flush.ok && m_storage.finalizeTypedSession(&error, withWriterDiagnostics(diagnostics));

    StorageUpdate update;
    update.ok = ok;
    update.error = ok ? QString() : QStringLiteral("Typed storage finalize failed: %1%2")
        .arg(flush.error)
        .arg(error.isEmpty() ? QString() : (flush.error.isEmpty() ? error : QStringLiteral("; %1").arg(error)));
    update.stateChanged = true;
    update.active = false;
    update.path = path;
    update.progressDue = true;
    update.bytesWritten = bytesWritten;
    update.recordCount = recordCount;
    resetQueue();
    return update;
}

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterRuntime::enqueueFrames(TypedCaptureFrameList frames) {
    StorageUpdate update;
    if (!m_storage.isActive() || frames.isEmpty()) return update;

    quint64 incomingBytes = 0;
    for (const TypedCaptureFrame& frame : frames) incomingBytes += quint64(frame.frameBytes.size());
    if (incomingBytes + m_queuedBytes > kWriterQueueMaxBytes) {
        m_captureInvalid = true;
        m_overrunRecords += quint64(frames.size());
        m_overrunBytes += incomingBytes;
        update.ok = false;
        update.error = QStringLiteral("Typed capture writer queue overrun: %1 bytes").arg(m_overrunBytes);
        return update;
    }

    m_queue.reserve(m_queue.size() + frames.size());
    for (TypedCaptureFrame& frame : frames) m_queue.push_back(std::move(frame));
    m_queuedBytes += incomingBytes;
    m_maxQueuedBytes = std::max(m_maxQueuedBytes, m_queuedBytes);
    return flushQueued(false);
}

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterRuntime::noteOverrun(quint64 records,
                                                                                quint64 bytes,
                                                                                const QString& reason) {
    m_captureInvalid = true;
    m_overrunRecords += records;
    m_overrunBytes += bytes;

    StorageUpdate update;
    update.ok = false;
    update.error = reason.isEmpty()
        ? QStringLiteral("Typed capture writer queue overrun: %1 bytes").arg(m_overrunBytes)
        : reason;
    return update;
}

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterRuntime::flushQueued(bool force) {
    StorageUpdate update;
    if (!m_storage.isActive()) return update;
    if (m_queue.isEmpty()) {
        if (storageProgressDue()) return makeStorageUpdate(false, true, m_storage.paths().sessionDir, true);
        return update;
    }

    QElapsedTimer timer;
    timer.start();
    const int takeCount = force ? m_queue.size() : std::min<int>(m_queue.size(), kWriterFlushMaxRecords);
    QString error;
    int writtenCount = 0;
    for (int index = 0; index < takeCount; ++index) {
        const TypedCaptureFrame& frame = m_queue.at(index);
        if (!m_storage.appendTypedCaptureFrame(frame, &error)) {
            m_captureInvalid = true;
            update.ok = false;
            update.error = QStringLiteral("Typed storage append failed: %1").arg(error);
            break;
        }
        m_queuedBytes -= quint64(frame.frameBytes.size());
        ++writtenCount;
    }
    if (writtenCount > 0) {
        m_queue.erase(m_queue.begin(), m_queue.begin() + writtenCount);
    }
    m_writeMaxMs = std::max<quint64>(m_writeMaxMs, quint64(timer.elapsed()));
    if (update.ok && storageProgressDue()) {
        update = makeStorageUpdate(false, true, m_storage.paths().sessionDir, true);
    }
    return update;
}

void TypedCaptureWriterRuntime::resetQueue() {
    m_queue.clear();
    m_queuedBytes = 0;
    m_maxQueuedBytes = 0;
    m_overrunRecords = 0;
    m_overrunBytes = 0;
    m_writeMaxMs = 0;
    m_captureInvalid = false;
}

TypedCaptureWriterRuntime::Status TypedCaptureWriterRuntime::status() const {
    Status status;
    status.active = m_storage.isActive();
    status.captureInvalid = m_captureInvalid;
    status.queuedRecords = quint64(m_queue.size());
    status.queuedBytes = m_queuedBytes;
    status.maxQueuedBytes = m_maxQueuedBytes;
    status.overrunRecords = m_overrunRecords;
    status.overrunBytes = m_overrunBytes;
    status.writeMaxMs = m_writeMaxMs;
    return status;
}

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterRuntime::makeStorageUpdate(bool stateChanged,
                                                                                      bool active,
                                                                                      const QString& path,
                                                                                      bool progressDue) const {
    StorageUpdate update;
    update.stateChanged = stateChanged;
    update.active = active;
    update.path = path;
    update.progressDue = progressDue;
    update.bytesWritten = m_storage.bytesWritten();
    update.recordCount = m_storage.recordCount();
    return update;
}

bool TypedCaptureWriterRuntime::storageProgressDue() {
    const quint64 records = m_storage.recordCount();
    const bool byCount = records >= m_lastReportedStorageRecordCount + kTypedStorageProgressRecordStep;
    const bool byTime = !m_storageProgressTimer.isValid() || m_storageProgressTimer.elapsed() >= kTypedStorageProgressIntervalMs;
    if (!byCount && !byTime) return false;
    m_lastReportedStorageRecordCount = records;
    m_storageProgressTimer.restart();
    return true;
}

QJsonObject TypedCaptureWriterRuntime::withWriterDiagnostics(const QJsonObject& diagnostics) const {
    QJsonObject root = diagnostics;
    QJsonObject writer;
    const Status s = status();
    writer.insert(QStringLiteral("active"), s.active);
    writer.insert(QStringLiteral("capture_invalid"), s.captureInvalid);
    writer.insert(QStringLiteral("queued_records"), QString::number(s.queuedRecords));
    writer.insert(QStringLiteral("queued_bytes"), QString::number(s.queuedBytes));
    writer.insert(QStringLiteral("max_queued_bytes"), QString::number(s.maxQueuedBytes));
    writer.insert(QStringLiteral("overrun_records"), QString::number(s.overrunRecords));
    writer.insert(QStringLiteral("overrun_bytes"), QString::number(s.overrunBytes));
    writer.insert(QStringLiteral("write_max_ms"), QString::number(s.writeMaxMs));
    root.insert(QStringLiteral("capture_writer"), writer);
    return root;
}

} // namespace CanMonitorTransport
