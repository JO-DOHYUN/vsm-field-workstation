#include "transport/HostTxQueue.h"

namespace CanMonitorTransport {

HostTxQueue::HostTxQueue(qsizetype maxFrames, qsizetype maxBytes)
    : m_maxFrames(maxFrames),
      m_maxBytes(maxBytes) {}

bool HostTxQueue::enqueue(const QByteArray& frame, const QString& summary, QString* error) {
    if (frame.isEmpty()) {
        if (error) *error = QStringLiteral("empty host frame");
        ++m_droppedFrames;
        return false;
    }
    if (m_items.size() >= m_maxFrames) {
        if (error) *error = QStringLiteral("host TX queue full");
        ++m_droppedFrames;
        return false;
    }
    if (m_queuedBytes + frame.size() > m_maxBytes) {
        if (error) *error = QStringLiteral("host TX queue byte limit exceeded");
        ++m_droppedFrames;
        return false;
    }

    Item item;
    item.sequence = m_nextSequence++;
    item.frame = frame;
    item.summary = summary;
    m_items.enqueue(item);
    m_queuedBytes += frame.size();
    ++m_enqueuedFrames;
    if (error) error->clear();
    return true;
}

HostTxQueue::Item HostTxQueue::dequeue() {
    if (m_items.isEmpty()) return {};
    Item item = m_items.dequeue();
    m_queuedBytes -= item.frame.size();
    return item;
}

void HostTxQueue::clear() {
    m_items.clear();
    m_queuedBytes = 0;
}

} // namespace CanMonitorTransport
