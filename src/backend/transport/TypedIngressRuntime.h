#pragma once

#include "../TypedRecords.h"
#include "../TypedTransportParser.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

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
    };

    void resetStreamState();

    IngestResult ingest(const QByteArray& bytes, qint64 handshakeElapsedMs, bool includeFrameBytes = false);
    IngestResult ingestEach(const QByteArray& bytes,
                            qint64 handshakeElapsedMs,
                            bool includeFrameBytes,
                            const std::function<void(TypedRecord&&)>& onRecord);
    HandshakeWatchdogState evaluateHandshake(qint64 elapsedMs, qint64 timeoutMs) const;
    QJsonObject makeCaptureDiagnostics() const;

private:
    StatusSnapshot makeStatusSnapshot() const;
    bool countersChanged(const TypedTransportParser::Counters& before) const;
    bool statusDue();

    TypedTransportParser m_parser;
    QElapsedTimer m_statusTimer;
    quint64 m_bytesSinceOpen = 0;
    bool m_capabilitySeenSinceOpen = false;
    int m_statusMinIntervalMs = 250;
};

} // namespace CanMonitorTransport
