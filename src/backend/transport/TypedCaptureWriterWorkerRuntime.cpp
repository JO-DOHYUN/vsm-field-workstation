#include "transport/TypedCaptureWriterWorkerRuntime.h"

#include <QMetaObject>

#include <utility>

namespace CanMonitorTransport {

TypedCaptureWriterWorkerRuntime::TypedCaptureWriterWorkerRuntime(QObject* parent)
    : QObject(parent) {}

void TypedCaptureWriterWorkerRuntime::setRecordQueue(QSharedPointer<TypedRecordHandoffQueue> queue) {
    m_recordQueue = std::move(queue);
}

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterWorkerRuntime::startStorageSync(const QString& sessionDir,
                                                                                           const QJsonObject& metadata) {
    const auto update = m_writer.startStorage(sessionDir, metadata);
    emitStatus(m_writer.status());
    return update;
}

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterWorkerRuntime::stopStorageSync(const QString& inactivePath,
                                                                                          const QJsonObject& diagnostics) {
    const auto update = m_writer.stopStorage(inactivePath, diagnostics);
    emitStatus(m_writer.status());
    return update;
}

TypedCaptureWriterRuntime::StorageUpdate TypedCaptureWriterWorkerRuntime::finalizeStorageIfActiveSync(const QJsonObject& diagnostics) {
    const auto update = m_writer.finalizeStorageIfActive(diagnostics);
    emitStatus(m_writer.status());
    return update;
}

void TypedCaptureWriterWorkerRuntime::enqueueFrames(TypedCaptureFrameList frames) {
    const auto update = m_writer.enqueueFrames(std::move(frames));
    emitStorageUpdate(update);
    emitStatus(m_writer.status());
    emit batchFinished();
}

void TypedCaptureWriterWorkerRuntime::drainQueuedRecords() {
    if (!m_recordQueue) {
        emit batchFinished();
        return;
    }

    TypedCaptureFrameList frames = m_recordQueue->popFrames(4096, 4ULL * 1024ULL * 1024ULL);
    if (frames.isEmpty()) {
        emitStatus(m_writer.status());
        emit batchFinished();
        return;
    }

    const auto update = m_writer.enqueueFrames(std::move(frames));
    emitStorageUpdate(update);
    emitStatus(m_writer.status());
    emit batchFinished();

    if (m_recordQueue && m_recordQueue->hasQueuedRecords()) {
        QMetaObject::invokeMethod(this, &TypedCaptureWriterWorkerRuntime::drainQueuedRecords, Qt::QueuedConnection);
    }
}

void TypedCaptureWriterWorkerRuntime::noteOverrun(quint64 records, quint64 bytes, const QString& reason) {
    const auto update = m_writer.noteOverrun(records, bytes, reason);
    emitStorageUpdate(update);
    emitStatus(m_writer.status());
}

void TypedCaptureWriterWorkerRuntime::resetQueue() {
    m_writer.resetQueue();
    emitStatus(m_writer.status());
}

void TypedCaptureWriterWorkerRuntime::emitCurrentStatus() {
    emitStatus(m_writer.status());
}

void TypedCaptureWriterWorkerRuntime::emitStorageUpdate(const TypedCaptureWriterRuntime::StorageUpdate& update) {
    if (update.ok &&
        !update.stateChanged &&
        !update.progressDue &&
        update.error.isEmpty()) {
        return;
    }
    emit storageUpdate(update.ok,
                       update.error,
                       update.stateChanged,
                       update.active,
                       update.path,
                       update.progressDue,
                       update.bytesWritten,
                       update.recordCount);
}

void TypedCaptureWriterWorkerRuntime::emitStatus(const TypedCaptureWriterRuntime::Status& status) {
    emit statusChanged(status.active,
                       status.captureInvalid,
                       status.queuedRecords,
                       status.queuedBytes,
                       status.maxQueuedBytes,
                       status.overrunRecords,
                       status.overrunBytes,
                       status.writeMaxMs);
}

} // namespace CanMonitorTransport
