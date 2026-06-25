#pragma once

#include "transport/TypedCaptureWriterRuntime.h"
#include "transport/TypedRecordHandoffQueue.h"

#include <QObject>
#include <QSharedPointer>

namespace CanMonitorTransport {

class TypedCaptureWriterWorkerRuntime : public QObject {
    Q_OBJECT
public:
    explicit TypedCaptureWriterWorkerRuntime(QObject* parent = nullptr);

    void setRecordQueue(QSharedPointer<TypedRecordHandoffQueue> queue);
    TypedCaptureWriterRuntime::StorageUpdate startStorageSync(const QString& sessionDir, const QJsonObject& metadata);
    TypedCaptureWriterRuntime::StorageUpdate stopStorageSync(const QString& inactivePath, const QJsonObject& diagnostics);
    TypedCaptureWriterRuntime::StorageUpdate finalizeStorageIfActiveSync(const QJsonObject& diagnostics);
    TypedCaptureWriterRuntime::Status status() const { return m_writer.status(); }

public slots:
    void enqueueRecords(TypedRecordList records);
    void drainQueuedRecords();
    void noteOverrun(quint64 records, quint64 bytes, const QString& reason);
    void resetQueue();
    void emitCurrentStatus();

signals:
    void storageUpdate(bool ok,
                       const QString& error,
                       bool stateChanged,
                       bool active,
                       const QString& path,
                       bool progressDue,
                       quint64 bytesWritten,
                       quint64 recordCount);
    void statusChanged(bool active,
                       bool captureInvalid,
                       quint64 queuedRecords,
                       quint64 queuedBytes,
                       quint64 maxQueuedBytes,
                       quint64 overrunRecords,
                       quint64 overrunBytes,
                       quint64 writeMaxMs);
    void batchFinished();

private:
    void emitStorageUpdate(const TypedCaptureWriterRuntime::StorageUpdate& update);
    void emitStatus(const TypedCaptureWriterRuntime::Status& status);

    TypedCaptureWriterRuntime m_writer;
    QSharedPointer<TypedRecordHandoffQueue> m_recordQueue;
};

} // namespace CanMonitorTransport
