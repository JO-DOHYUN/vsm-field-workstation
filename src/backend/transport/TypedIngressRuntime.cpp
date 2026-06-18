#include "transport/TypedIngressRuntime.h"

#include <algorithm>

namespace {
constexpr int kTypedLiveEmitBatchSize = 512;
constexpr quint64 kTypedStorageProgressRecordStep = 1536;
constexpr int kTypedStorageProgressIntervalMs = 1000;
}

namespace CanMonitorTransport {

void TypedIngressRuntime::resetStreamState() {
    m_parser.reset();
    m_bytesSinceOpen = 0;
    m_capabilitySeenSinceOpen = false;
    m_statusTimer.invalidate();
}

TypedIngressRuntime::StorageUpdate TypedIngressRuntime::startStorage(const QString& sessionDir, const QJsonObject& metadata) {
    if (m_storage.isActive()) {
        StorageUpdate update;
        update.ok = false;
        update.error = QStringLiteral("Typed storage is already active.");
        return update;
    }

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

TypedIngressRuntime::StorageUpdate TypedIngressRuntime::stopStorage(const QString& inactivePath) {
    if (!m_storage.isActive()) {
        return makeStorageUpdate(true, false, inactivePath, false);
    }
    return finalizeStorageIfActive();
}

TypedIngressRuntime::StorageUpdate TypedIngressRuntime::finalizeStorageIfActive() {
    if (!m_storage.isActive()) return {};

    const QString path = m_storage.paths().sessionDir;
    QString error;
    const quint64 bytesWritten = m_storage.bytesWritten();
    const quint64 recordCount = m_storage.recordCount();
    const bool ok = m_storage.finalizeTypedSession(&error, makeCaptureDiagnostics());

    StorageUpdate update;
    update.ok = ok;
    update.error = ok ? QString() : QStringLiteral("Typed storage finalize failed: %1").arg(error);
    update.stateChanged = true;
    update.active = false;
    update.path = path;
    update.progressDue = true;
    update.bytesWritten = bytesWritten;
    update.recordCount = recordCount;
    return update;
}

TypedIngressRuntime::IngestResult TypedIngressRuntime::ingest(const QByteArray& bytes, qint64 handshakeElapsedMs) {
    IngestResult result;
    if (bytes.isEmpty()) {
        result.status = makeStatusSnapshot();
        return result;
    }

    m_bytesSinceOpen += quint64(bytes.size());
    const TypedTransportParser::Counters countersBefore = m_parser.counters();
    m_parser.append(bytes);

    TypedRecordList batch;
    batch.reserve(kTypedLiveEmitBatchSize);
    auto flushBatch = [&result, &batch]() {
        if (batch.isEmpty()) return;
        result.recordBatches.push_back(batch);
        batch.clear();
        batch.reserve(kTypedLiveEmitBatchSize);
    };

    while (true) {
        auto record = m_parser.takeOne();
        if (!record) break;
        if (!m_capabilitySeenSinceOpen && record->isType(TypedRecordType::Capability)) {
            m_capabilitySeenSinceOpen = true;
            result.capabilityFirstSeen = true;
            result.capabilityElapsedMs = handshakeElapsedMs;
            result.capabilityBytes = m_bytesSinceOpen;
        }
        if (m_storage.isActive()) {
            QString error;
            if (!m_storage.appendTypedRecord(*record, &error)) {
                result.errors.push_back(QStringLiteral("Typed storage append failed: %1").arg(error));
            }
        }
        batch.push_back(*record);
        if (batch.size() >= kTypedLiveEmitBatchSize) flushBatch();
    }

    flushBatch();
    result.status = makeStatusSnapshot();
    result.statusDue = countersChanged(countersBefore) && statusDue();
    if (m_storage.isActive() && storageProgressDue()) {
        result.storageProgressDue = true;
        result.storageBytesWritten = m_storage.bytesWritten();
        result.storageRecordCount = m_storage.recordCount();
    }
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

TypedIngressRuntime::StorageUpdate TypedIngressRuntime::makeStorageUpdate(bool stateChanged,
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

bool TypedIngressRuntime::storageProgressDue() {
    const quint64 records = m_storage.recordCount();
    const bool byCount = records >= m_lastReportedStorageRecordCount + kTypedStorageProgressRecordStep;
    const bool byTime = !m_storageProgressTimer.isValid() || m_storageProgressTimer.elapsed() >= kTypedStorageProgressIntervalMs;
    if (!byCount && !byTime) return false;
    m_lastReportedStorageRecordCount = records;
    m_storageProgressTimer.restart();
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

    QJsonObject storage;
    storage.insert(QStringLiteral("active"), m_storage.isActive());
    storage.insert(QStringLiteral("record_count"), QString::number(m_storage.recordCount()));
    storage.insert(QStringLiteral("bytes_written"), QString::number(m_storage.bytesWritten()));

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("typed-capture-diagnostics-v1"));
    root.insert(QStringLiteral("parser"), parser);
    root.insert(QStringLiteral("storage"), storage);
    root.insert(QStringLiteral("bytes_since_open"), QString::number(m_bytesSinceOpen));
    root.insert(QStringLiteral("capability_seen_since_open"), m_capabilitySeenSinceOpen);
    return root;
}

} // namespace CanMonitorTransport
