#include "transport/HostTxRuntime.h"

namespace CanMonitorTransport {

HostTxRuntime::HostTxRuntime(qsizetype maxInFlightBytes)
    : m_maxInFlightBytes(maxInFlightBytes) {}

HostTxRuntime::EnqueueResult HostTxRuntime::enqueue(const QByteArray& frame, const QString& summary) {
    QString error;
    const QString normalizedSummary = summary.isEmpty() ? QStringLiteral("host frame") : summary;
    const bool ok = m_queue.enqueue(frame, normalizedSummary, &error);
    return {ok, error, status()};
}

std::optional<HostTxRuntime::WriteItem> HostTxRuntime::takeNextForWrite(qsizetype serialBytesToWrite) {
    if (!m_queue.hasPending() || serialBytesToWrite >= m_maxInFlightBytes) return std::nullopt;
    const auto item = m_queue.dequeue();
    return WriteItem{item.frame, item.summary.isEmpty() ? QStringLiteral("host frame") : item.summary};
}

void HostTxRuntime::markWritten() {
    m_queue.markWritten();
}

HostTxRuntime::ClearResult HostTxRuntime::clear(const QString& reason) {
    ClearResult result;
    result.hadPending = m_queue.hasPending();
    if (result.hadPending) {
        const quint64 dropped = quint64(m_queue.queuedFrames());
        m_queue.clear();
        result.error = QStringLiteral("Host TX queue cleared: %1 · %2 frame(s)").arg(reason).arg(dropped);
    }
    result.status = status();
    return result;
}

HostTxRuntime::Status HostTxRuntime::status() const {
    return {
        quint64(m_queue.queuedFrames()),
        quint64(m_queue.queuedBytes()),
        m_queue.enqueuedFrames(),
        m_queue.writtenFrames(),
        m_queue.droppedFrames(),
    };
}

} // namespace CanMonitorTransport
