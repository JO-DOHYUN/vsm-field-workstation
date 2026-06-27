#pragma once

#include "../StorageRuntime.h"
#include "../TypedRecords.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CanMonitorTransport {

class TypedCaptureWriterRuntime {
public:
    struct StorageUpdate {
        bool ok = true;
        QString error;
        bool stateChanged = false;
        bool active = false;
        QString path;
        bool progressDue = false;
        quint64 bytesWritten = 0;
        quint64 recordCount = 0;
    };

    struct Status {
        bool active = false;
        bool captureInvalid = false;
        quint64 queuedRecords = 0;
        quint64 queuedBytes = 0;
        quint64 maxQueuedBytes = 0;
        quint64 overrunRecords = 0;
        quint64 overrunBytes = 0;
        quint64 writeMaxMs = 0;
    };

    StorageUpdate startStorage(const QString& sessionDir, const QJsonObject& metadata);
    StorageUpdate stopStorage(const QString& inactivePath, const QJsonObject& diagnostics);
    StorageUpdate finalizeStorageIfActive(const QJsonObject& diagnostics);

    StorageUpdate enqueueFrames(TypedCaptureFrameList frames);
    StorageUpdate noteOverrun(quint64 records, quint64 bytes, const QString& reason);
    StorageUpdate flushQueued(bool force = false);
    void resetQueue();
    Status status() const;
    bool isActive() const { return m_storage.isActive(); }

private:
    StorageUpdate makeStorageUpdate(bool stateChanged, bool active, const QString& path, bool progressDue) const;
    bool storageProgressDue();
    QJsonObject withWriterDiagnostics(const QJsonObject& diagnostics) const;

    StorageRuntime m_storage;
    QVector<TypedCaptureFrame> m_queue;
    quint64 m_queuedBytes = 0;
    quint64 m_maxQueuedBytes = 0;
    quint64 m_overrunRecords = 0;
    quint64 m_overrunBytes = 0;
    quint64 m_writeMaxMs = 0;
    quint64 m_lastReportedStorageRecordCount = 0;
    bool m_captureInvalid = false;
    QElapsedTimer m_storageProgressTimer;
};

} // namespace CanMonitorTransport
