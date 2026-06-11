#pragma once

#include <QByteArray>
#include <QString>
#include <QQueue>
#include <QtGlobal>

namespace CanMonitorTransport {

class HostTxQueue {
public:
    struct Item {
        quint64 sequence = 0;
        QByteArray frame;
        QString summary;
    };

    explicit HostTxQueue(qsizetype maxFrames = 256, qsizetype maxBytes = 64 * 1024);

    bool enqueue(const QByteArray& frame, const QString& summary, QString* error = nullptr);
    bool hasPending() const { return !m_items.isEmpty(); }
    Item dequeue();
    void clear();

    qsizetype queuedFrames() const { return m_items.size(); }
    qsizetype queuedBytes() const { return m_queuedBytes; }
    quint64 enqueuedFrames() const { return m_enqueuedFrames; }
    quint64 writtenFrames() const { return m_writtenFrames; }
    quint64 droppedFrames() const { return m_droppedFrames; }
    qsizetype maxFrames() const { return m_maxFrames; }
    qsizetype maxBytes() const { return m_maxBytes; }

    void markWritten() { ++m_writtenFrames; }

private:
    qsizetype m_maxFrames = 256;
    qsizetype m_maxBytes = 64 * 1024;
    qsizetype m_queuedBytes = 0;
    quint64 m_nextSequence = 1;
    quint64 m_enqueuedFrames = 0;
    quint64 m_writtenFrames = 0;
    quint64 m_droppedFrames = 0;
    QQueue<Item> m_items;
};

} // namespace CanMonitorTransport
