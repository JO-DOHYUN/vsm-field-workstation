#include "transport/TypedEvidencePipelineRuntime.h"

#include <algorithm>

namespace CanMonitorTransport {

void TypedEvidencePipelineRuntime::reset() {
    m_ingress.resetStreamState();
    m_status = {};
}

TypedIngressRuntime::IngestResult TypedEvidencePipelineRuntime::ingestBlocks(const QVector<DrainByteQueue::Block>& blocks,
                                                                             qint64 handshakeElapsedMs,
                                                                             quint64 parseBacklogBytes) {
    TypedIngressRuntime::IngestResult aggregate;
    m_status.parseBacklogBytes = parseBacklogBytes;
    if (blocks.isEmpty()) {
        aggregate.status = m_ingress.ingest(QByteArray(), handshakeElapsedMs).status;
        return aggregate;
    }

    QElapsedTimer timer;
    timer.start();
    for (const DrainByteQueue::Block& block : blocks) {
        m_status.ingestedBlocks += 1;
        m_status.ingestedBytes += quint64(block.bytes.size());
        auto result = m_ingress.ingest(block.bytes, handshakeElapsedMs);
        aggregate.recordBatches += result.recordBatches;
        aggregate.errors += result.errors;
        aggregate.capabilityFirstSeen = aggregate.capabilityFirstSeen || result.capabilityFirstSeen;
        if (result.capabilityFirstSeen) {
            aggregate.capabilityElapsedMs = result.capabilityElapsedMs;
            aggregate.capabilityBytes = result.capabilityBytes;
        }
        aggregate.statusDue = aggregate.statusDue || result.statusDue;
        aggregate.status = result.status;
    }
    m_status.parserBatchMaxMs = std::max<quint64>(m_status.parserBatchMaxMs, quint64(timer.elapsed()));
    return aggregate;
}

TypedIngressRuntime::HandshakeWatchdogState TypedEvidencePipelineRuntime::evaluateHandshake(qint64 elapsedMs,
                                                                                           qint64 timeoutMs) const {
    return m_ingress.evaluateHandshake(elapsedMs, timeoutMs);
}

QJsonObject TypedEvidencePipelineRuntime::makeCaptureDiagnostics() const {
    QJsonObject root = m_ingress.makeCaptureDiagnostics();
    QJsonObject pipeline;
    pipeline.insert(QStringLiteral("parse_backlog_bytes"), QString::number(m_status.parseBacklogBytes));
    pipeline.insert(QStringLiteral("parser_batch_max_ms"), QString::number(m_status.parserBatchMaxMs));
    pipeline.insert(QStringLiteral("ingested_blocks"), QString::number(m_status.ingestedBlocks));
    pipeline.insert(QStringLiteral("ingested_bytes"), QString::number(m_status.ingestedBytes));
    root.insert(QStringLiteral("typed_pipeline"), pipeline);
    return root;
}

} // namespace CanMonitorTransport
