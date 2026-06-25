#include "transport/TypedIngressRuntime.h"

#include <algorithm>
#include <utility>

namespace CanMonitorTransport {

void TypedIngressRuntime::resetStreamState() {
    m_parser.reset();
    m_bytesSinceOpen = 0;
    m_capabilitySeenSinceOpen = false;
    m_statusTimer.invalidate();
}

TypedIngressRuntime::IngestResult TypedIngressRuntime::ingest(const QByteArray& bytes, qint64 handshakeElapsedMs) {
    IngestResult result = ingestEach(bytes, handshakeElapsedMs, [&result](TypedRecord&& record) {
        static constexpr int kTypedLiveEmitBatchSize = 64;
        if (result.recordBatches.isEmpty() || result.recordBatches.last().size() >= kTypedLiveEmitBatchSize) {
            result.recordBatches.push_back(TypedRecordList{});
            result.recordBatches.last().reserve(kTypedLiveEmitBatchSize);
        }
        TypedRecord liveRecord;
        liveRecord.header = record.header;
        liveRecord.payload = std::move(record.payload);
        result.recordBatches.last().push_back(std::move(liveRecord));
    });
    return result;
}

TypedIngressRuntime::IngestResult TypedIngressRuntime::ingestEach(const QByteArray& bytes,
                                                                  qint64 handshakeElapsedMs,
                                                                  const std::function<void(TypedRecord&&)>& onRecord) {
    IngestResult result;
    if (bytes.isEmpty()) {
        result.status = makeStatusSnapshot();
        return result;
    }

    m_bytesSinceOpen += quint64(bytes.size());
    const TypedTransportParser::Counters countersBefore = m_parser.counters();
    m_parser.append(bytes);

    while (true) {
        auto record = m_parser.takeOne(false);
        if (!record) break;
        if (!m_capabilitySeenSinceOpen && record->isType(TypedRecordType::Capability)) {
            m_capabilitySeenSinceOpen = true;
            result.capabilityFirstSeen = true;
            result.capabilityElapsedMs = handshakeElapsedMs;
            result.capabilityBytes = m_bytesSinceOpen;
        }
        if (onRecord) onRecord(std::move(*record));
    }

    result.status = makeStatusSnapshot();
    result.statusDue = countersChanged(countersBefore) && statusDue();
    return result;
}

TypedIngressRuntime::HandshakeWatchdogState TypedIngressRuntime::evaluateHandshake(qint64 elapsedMs, qint64 timeoutMs) const {
    HandshakeWatchdogState state;
    state.capabilitySeen = m_capabilitySeenSinceOpen;
    if (state.capabilitySeen || elapsedMs < 0 || elapsedMs < timeoutMs) return state;

    state.timedOut = true;
    state.timeoutReason = QStringLiteral("typed handshake timeout: no CAPABILITY after %1ms, bytes=%2, frames=%3")
        .arg(elapsedMs)
        .arg(m_bytesSinceOpen)
        .arg(m_parser.counters().frames);
    return state;
}

TypedIngressRuntime::StatusSnapshot TypedIngressRuntime::makeStatusSnapshot() const {
    const auto counters = m_parser.counters();
    return {
        counters.frames,
        counters.bytesDropped,
        counters.crcFailures,
        counters.lengthFailures,
        counters.versionWarnings,
        counters.seqGaps,
    };
}

bool TypedIngressRuntime::countersChanged(const TypedTransportParser::Counters& before) const {
    const auto& after = m_parser.counters();
    return after.frames != before.frames
        || after.bytesDropped != before.bytesDropped
        || after.crcFailures != before.crcFailures
        || after.lengthFailures != before.lengthFailures
        || after.versionWarnings != before.versionWarnings
        || after.seqGaps != before.seqGaps;
}

bool TypedIngressRuntime::statusDue() {
    if (m_statusTimer.isValid() && m_statusTimer.elapsed() < m_statusMinIntervalMs) return false;
    m_statusTimer.restart();
    return true;
}

QJsonObject TypedIngressRuntime::makeCaptureDiagnostics() const {
    const auto counters = m_parser.counters();
    QJsonObject parser;
    parser.insert(QStringLiteral("frames"), QString::number(counters.frames));
    parser.insert(QStringLiteral("bytes_dropped"), QString::number(counters.bytesDropped));
    parser.insert(QStringLiteral("crc_failures"), QString::number(counters.crcFailures));
    parser.insert(QStringLiteral("length_failures"), QString::number(counters.lengthFailures));
    parser.insert(QStringLiteral("version_warnings"), QString::number(counters.versionWarnings));
    parser.insert(QStringLiteral("seq_gaps"), QString::number(counters.seqGaps));
    parser.insert(QStringLiteral("buffered_bytes"), QString::number(quint64(std::max<qsizetype>(0, m_parser.bufferedBytes()))));

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("typed-capture-diagnostics-v1"));
    root.insert(QStringLiteral("parser"), parser);
    root.insert(QStringLiteral("bytes_since_open"), QString::number(m_bytesSinceOpen));
    root.insert(QStringLiteral("capability_seen_since_open"), m_capabilitySeenSinceOpen);
    return root;
}

} // namespace CanMonitorTransport
