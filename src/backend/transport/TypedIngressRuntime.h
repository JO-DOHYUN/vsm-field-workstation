#pragma once

#include "../StorageRuntime.h"
#include "../TypedRecords.h"
#include "../TypedTransportParser.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CanMonitorTransport {

class TypedIngressRuntime {
public:
    struct StatusSnapshot {
        quint64 frames = 0;
        quint64 bytesDropped = 0;
        quint64 crcFailures = 0;
        quint64 lengthFailures = 0;
        quint64 versionWarnings = 0;
        quint64 seqGaps = 0;
    };

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

    struct HandshakeWatchdogState {
        bool capabilitySeen = false;
        bool timedOut = false;
        QString timeoutReason;
    };

    struct IngestResult {
        QVector<TypedRecordList> recordBatches;
        QStringList errors;
        bool capabilityFirstSeen = false;
        qint64 capabilityElapsedMs = -1;
        quint64 capabilityBytes = 0;
        bool statusDue = false;
        StatusSnapshot status;
        bool storageProgressDue = false;
        quint64 storageBytesWritten = 0;
        quint64 storageRecordCount = 0;
    };

    void resetStreamState();

    StorageUpdate startStorage(const QString& sessionDir, const QJsonObject& metadata);
    StorageUpdate stopStorage(const QString& inactivePath = QString());
    StorageUpdate finalizeStorageIfActive();

    IngestResult ingest(const QByteArray& bytes, qint64 handshakeElapsedMs);
    HandshakeWatchdogState evaluateHandshake(qint64 elapsedMs, qint64 timeoutMs) const;

private:
    StatusSnapshot makeStatusSnapshot() const;
    StorageUpdate makeStorageUpdate(bool stateChanged, bool active, const QString& path, bool progressDue) const;
    bool countersChanged(const TypedTransportParser::Counters& before) const;
    bool statusDue();
    bool storageProgressDue();

    TypedTransportParser m_parser;
    StorageRuntime m_storage;
    QElapsedTimer m_statusTimer;
    QElapsedTimer m_storageProgressTimer;
    quint64 m_lastReportedStorageRecordCount = 0;
    quint64 m_bytesSinceOpen = 0;
    bool m_capabilitySeenSinceOpen = false;
    int m_statusMinIntervalMs = 100;
};

} // namespace CanMonitorTransport
