#include "transport/TypedCaptureWriterWorkerRuntime.h"

namespace CanMonitorTransport {

TypedCaptureWriterWorkerRuntime::TypedCaptureWriterWorkerRuntime(QObject* parent)
    : QObject(parent) {}

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

void TypedCaptureWriterWorkerRuntime::enqueueRecords(TypedRecordList records) {
    const auto update = m_writer.enqueueRecords(records);
    emitStorageUpdate(update);
    emitStatus(m_writer.status());
    emit batchFinished();
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
