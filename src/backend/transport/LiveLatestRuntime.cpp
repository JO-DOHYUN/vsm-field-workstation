#include "transport/LiveLatestRuntime.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr int kLatestFlushIntervalMs = 250;
constexpr int kLatestStatusIntervalMs = 500;
}

namespace CanMonitorTransport {

void LiveLatestRuntime::reset() {
    m_pendingFramesByKey.clear();
    m_lastMonoUsByKey.clear();
    m_flushClock.invalidate();
    m_statusClock.invalidate();
    m_status = {};
    m_lastStatusDisplayLoss = 0;
}

LiveLatestRuntime::IngestResult LiveLatestRuntime::ingestRecord(const TypedRecord& record) {
    IngestResult result;
    m_status.lastInputRecords = 1;
    m_status.lastOutputFrames = 0;

    if (record.isType(TypedRecordType::CanRxRaw)) {
        const auto can = decodeTypedCanRaw(record);
        if (can) {
            ingestFrame(toFrameRecord(record, *can));
        } else {
            ++m_status.displayLoss;
        }
    } else if (record.isType(TypedRecordType::CanRxSegment)) {
        const auto header = decodeTypedCanRxSegmentHeader(record);
        if (header) {
            for (qsizetype index = 0; index < header->frameCount; ++index) {
                const auto entry = decodeTypedCanRxSegmentEntry(record, index);
                if (!entry) {
                    ++m_status.displayLoss;
                    continue;
                }
                ingestFrame(toFrameRecord(record, *entry));
            }
        } else {
            ++m_status.displayLoss;
        }
    }

    if (flushDue()) {
        result.frames = flush(false);
    }
    result.statusDue = statusDue() || !result.frames.isEmpty();
    if (result.statusDue) {
        m_statusClock.restart();
        m_lastStatusDisplayLoss = m_status.displayLoss;
    }
    result.status = status();
    return result;
}

FrameRecordList LiveLatestRuntime::flush(bool force) {
    if (m_pendingFramesByKey.isEmpty()) return {};
    if (!force && m_flushClock.isValid() && m_flushClock.elapsed() < kLatestFlushIntervalMs) return {};

    QElapsedTimer budget;
    budget.start();

    FrameRecordList frames;
    frames.reserve(m_pendingFramesByKey.size());
    for (auto it = m_pendingFramesByKey.cbegin(); it != m_pendingFramesByKey.cend(); ++it) {
        frames.push_back(it.value());
    }
    m_pendingFramesByKey.clear();

    std::sort(frames.begin(), frames.end(), [](const FrameRecord& a, const FrameRecord& b) {
        if (a.tExtUs != b.tExtUs) return a.tExtUs < b.tExtUs;
        if (a.bus != b.bus) return a.bus < b.bus;
        if (a.canId != b.canId) return a.canId < b.canId;
        if (a.ext != b.ext) return a.ext < b.ext;
        return a.rtr < b.rtr;
    });

    m_status.pendingKeys = 0;
    m_status.lastOutputFrames = frames.size();
    m_status.emittedLatestFrames += quint64(frames.size());
    ++m_status.flushCount;
    m_status.lastFlushMs = int(budget.elapsed());
    m_flushClock.restart();
    return frames;
}

void LiveLatestRuntime::ingestFrame(const FrameRecord& inputFrame) {
    FrameRecord frame = inputFrame;
    const quint64 key = keyForFrame(frame);
    const auto lastMonoIt = m_lastMonoUsByKey.constFind(key);
    if (lastMonoIt != m_lastMonoUsByKey.cend() && frame.tExtUs >= lastMonoIt.value()) {
        frame.hasObservedGap = true;
        frame.observedGapUs = frame.tExtUs - lastMonoIt.value();
    }
    m_lastMonoUsByKey.insert(key, frame.tExtUs);

    ++m_status.observedCanRxFrames;
    if (frame.bus == 0) ++m_status.observedBus0CanRxFrames;
    else if (frame.bus == 1) ++m_status.observedBus1CanRxFrames;

    auto pendingIt = m_pendingFramesByKey.find(key);
    if (pendingIt != m_pendingFramesByKey.end()) {
        if (pendingIt.value().hasObservedGap &&
            (!frame.hasObservedGap || pendingIt.value().observedGapUs > frame.observedGapUs)) {
            frame.hasObservedGap = true;
            frame.observedGapUs = pendingIt.value().observedGapUs;
        }
        pendingIt.value() = frame;
        ++m_status.coalescedLatestUpdates;
    } else {
        m_pendingFramesByKey.insert(key, frame);
    }
    m_status.pendingKeys = m_pendingFramesByKey.size();
    m_status.maxPendingKeys = std::max(m_status.maxPendingKeys, m_status.pendingKeys);
}

bool LiveLatestRuntime::flushDue() const {
    if (m_pendingFramesByKey.isEmpty()) return false;
    return !m_flushClock.isValid() || m_flushClock.elapsed() >= kLatestFlushIntervalMs;
}

int LiveLatestRuntime::flushIntervalMs() const {
    return kLatestFlushIntervalMs;
}

LiveLatestRuntime::Status LiveLatestRuntime::status() const {
    Status out = m_status;
    out.pendingKeys = m_pendingFramesByKey.size();
    return out;
}

quint64 LiveLatestRuntime::keyForFrame(const FrameRecord& frame) {
    quint64 key = (quint64(frame.bus) << 56);
    if (frame.ext) key |= (quint64(1) << 55);
    if (frame.rtr) key |= (quint64(1) << 54);
    key |= quint64(frame.canId & 0x1FFFFFFFU);
    return key;
}

FrameRecord LiveLatestRuntime::toFrameRecord(const TypedRecord& record, const TypedCanRawRecord& can) {
    FrameRecord frame;
    frame.tExtUs = can.monoUs;
    frame.canId = can.canId;
    frame.ext = can.extended;
    frame.rtr = can.rtr;
    frame.dlc = can.dlc;
    frame.bus = can.bus;
    frame.seq = quint8(record.header.seq & 0xFF);
    std::memcpy(frame.data, can.data, sizeof(frame.data));
    return frame;
}

FrameRecord LiveLatestRuntime::toFrameRecord(const TypedRecord& record, const TypedCanRxSegmentEntry& entry) {
    FrameRecord frame;
    frame.tExtUs = entry.monoUs;
    frame.canId = entry.canId;
    frame.ext = entry.extended;
    frame.rtr = entry.rtr;
    frame.dlc = entry.dlc;
    frame.bus = entry.bus;
    frame.seq = quint8(record.header.seq & 0xFF);
    frame.hasCaptureSeq = true;
    frame.captureSeq = entry.captureSeq;
    std::memcpy(frame.data, entry.data, sizeof(frame.data));
    return frame;
}

bool LiveLatestRuntime::statusDue() const {
    if (m_status.displayLoss != m_lastStatusDisplayLoss) return true;
    if (!m_statusClock.isValid()) return true;
    return m_statusClock.elapsed() >= kLatestStatusIntervalMs;
}

} // namespace CanMonitorTransport
