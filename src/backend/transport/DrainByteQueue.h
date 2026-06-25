#pragma once

#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>
#include <QtGlobal>

#include <deque>

namespace CanMonitorTransport {

class DrainByteQueue {
public:
    struct Block {
        QByteArray bytes;
        quint64 sequence = 0;
    };

    struct Snapshot {
        quint64 usedBytes = 0;
        quint64 maxUsedBytes = 0;
        quint64 capacityBytes = 0;
        quint64 pushedBlocks = 0;
        quint64 poppedBlocks = 0;
        quint64 overrunBytes = 0;
        quint64 contentionCount = 0;
    };

    explicit DrainByteQueue(qsizetype capacityBytes = 16 * 1024 * 1024);

    bool push(QByteArray bytes);
    QVector<Block> popAll(qsizetype maxBytes = -1);
    void reset();
    Snapshot snapshot() const;

private:
    mutable QMutex m_mutex;
    std::deque<Block> m_blocks;
    qsizetype m_capacityBytes = 0;
    qsizetype m_usedBytes = 0;
    quint64 m_nextSequence = 1;
    quint64 m_pushedBlocks = 0;
    quint64 m_poppedBlocks = 0;
    quint64 m_maxUsedBytes = 0;
    quint64 m_overrunBytes = 0;
    quint64 m_contentionCount = 0;
};

} // namespace CanMonitorTransport
