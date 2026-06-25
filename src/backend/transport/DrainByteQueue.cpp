#include "transport/DrainByteQueue.h"

#include <algorithm>

namespace CanMonitorTransport {

DrainByteQueue::DrainByteQueue(qsizetype capacityBytes)
    : m_capacityBytes(std::max<qsizetype>(capacityBytes, 1)) {}

bool DrainByteQueue::push(QByteArray bytes) {
    if (bytes.isEmpty()) return true;

    if (!m_mutex.tryLock()) {
        ++m_contentionCount;
        m_mutex.lock();
    }

    if (bytes.size() > m_capacityBytes || m_usedBytes + bytes.size() > m_capacityBytes) {
        m_overrunBytes += quint64(bytes.size());
        m_mutex.unlock();
        return false;
    }

    Block block;
    block.sequence = m_nextSequence++;
    block.bytes = std::move(bytes);
    m_usedBytes += block.bytes.size();
    m_blocks.push_back(std::move(block));
    ++m_pushedBlocks;
    m_maxUsedBytes = std::max<quint64>(m_maxUsedBytes, quint64(m_usedBytes));
    m_mutex.unlock();
    return true;
}

QVector<DrainByteQueue::Block> DrainByteQueue::popAll(qsizetype maxBytes) {
    if (!m_mutex.tryLock()) {
        ++m_contentionCount;
        m_mutex.lock();
    }

    QVector<Block> out;
    if (m_blocks.empty()) {
        m_mutex.unlock();
        return out;
    }

    qsizetype takenBytes = 0;
    qsizetype takeCount = 0;
    while (takeCount < qsizetype(m_blocks.size())) {
        const qsizetype blockBytes = m_blocks[size_t(takeCount)].bytes.size();
        if (maxBytes >= 0 && takenBytes > 0 && takenBytes + blockBytes > maxBytes) break;
        takenBytes += blockBytes;
        ++takeCount;
        if (maxBytes >= 0 && takenBytes >= maxBytes) break;
    }

    out.reserve(takeCount);
    for (qsizetype index = 0; index < takeCount; ++index) {
        out.push_back(std::move(m_blocks.front()));
        m_blocks.pop_front();
    }
    m_usedBytes -= takenBytes;
    m_poppedBlocks += quint64(takeCount);
    m_mutex.unlock();
    return out;
}

void DrainByteQueue::reset() {
    QMutexLocker locker(&m_mutex);
    m_blocks.clear();
    m_usedBytes = 0;
    m_nextSequence = 1;
    m_pushedBlocks = 0;
    m_poppedBlocks = 0;
    m_maxUsedBytes = 0;
    m_overrunBytes = 0;
    m_contentionCount = 0;
}

DrainByteQueue::Snapshot DrainByteQueue::snapshot() const {
    QMutexLocker locker(&m_mutex);
    Snapshot snapshot;
    snapshot.usedBytes = quint64(m_usedBytes);
    snapshot.maxUsedBytes = m_maxUsedBytes;
    snapshot.capacityBytes = quint64(m_capacityBytes);
    snapshot.pushedBlocks = m_pushedBlocks;
    snapshot.poppedBlocks = m_poppedBlocks;
    snapshot.overrunBytes = m_overrunBytes;
    snapshot.contentionCount = m_contentionCount;
    return snapshot;
}

} // namespace CanMonitorTransport
