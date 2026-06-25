#pragma once

#include "transport/DrainByteQueue.h"
#include "transport/TypedIngressRuntime.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QVector>

namespace CanMonitorTransport {

class TypedEvidencePipelineRuntime {
public:
    struct Status {
        quint64 parseBacklogBytes = 0;
        quint64 parserBatchMaxMs = 0;
        quint64 ingestedBlocks = 0;
        quint64 ingestedBytes = 0;
    };

    void reset();
    TypedIngressRuntime::IngestResult ingestBlocks(const QVector<DrainByteQueue::Block>& blocks,
                                                   qint64 handshakeElapsedMs,
                                                   quint64 parseBacklogBytes);
    TypedIngressRuntime::HandshakeWatchdogState evaluateHandshake(qint64 elapsedMs, qint64 timeoutMs) const;
    QJsonObject makeCaptureDiagnostics() const;
    Status status() const { return m_status; }

private:
    TypedIngressRuntime m_ingress;
    Status m_status;
};

} // namespace CanMonitorTransport
