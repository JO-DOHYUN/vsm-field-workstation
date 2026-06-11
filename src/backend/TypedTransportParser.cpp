#include "TypedTransportParser.h"

#include <algorithm>

void TypedTransportParser::reset() {
    m_buffer.clear();
    m_bufferOffset = 0;
    m_counters = {};
    m_haveLastSeq = false;
    m_lastSeq = 0;
}

void TypedTransportParser::append(const QByteArray& chunk) {
    compactBufferIfNeeded();
    m_buffer.append(chunk);
}

quint16 TypedTransportParser::crc16Ccitt(const quint8* data, qsizetype len) {
    quint16 crc = 0xFFFF;
    for (qsizetype index = 0; index < len; ++index) {
        crc ^= quint16(data[index]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
        }
    }
    return crc;
}

qsizetype TypedTransportParser::findSof(const QByteArray& buffer, qsizetype offset) {
    const int size = int(buffer.size());
    for (int index = int(offset); index + 1 < size; ++index) {
        const auto b0 = quint8(buffer.at(index));
        const auto b1 = quint8(buffer.at(index + 1));
        if (b0 == kTypedTransportSof0 && b1 == kTypedTransportSof1) return index;
    }
    return -1;
}

void TypedTransportParser::dropBufferedBytes(qsizetype count) {
    if (count <= 0) return;
    m_bufferOffset += std::min(count, bufferedBytes());
    compactBufferIfNeeded();
}

void TypedTransportParser::compactBufferIfNeeded(bool force) {
    if (m_bufferOffset <= 0) return;
    if (m_bufferOffset >= m_buffer.size()) {
        m_buffer.clear();
        m_bufferOffset = 0;
        return;
    }
    if (!force && m_bufferOffset < 65536 && (m_bufferOffset * 2) < m_buffer.size()) return;
    m_buffer.remove(0, m_bufferOffset);
    m_bufferOffset = 0;
}

bool TypedTransportParser::noteSequence(quint16 seq) {
    if (!m_haveLastSeq) {
        m_haveLastSeq = true;
        m_lastSeq = seq;
        return true;
    }
    const quint16 expected = quint16(m_lastSeq + 1);
    const bool contiguous = seq == expected;
    if (!contiguous) ++m_counters.seqGaps;
    m_lastSeq = seq;
    return contiguous;
}

std::optional<TypedRecord> TypedTransportParser::takeOne() {
    while (true) {
        if (bufferedBytes() < kTypedTransportFrameOverhead) return std::nullopt;

        const qsizetype sof = findSof(m_buffer, m_bufferOffset);
        if (sof < 0) {
            const qsizetype drop = std::max<qsizetype>(0, bufferedBytes() - 1);
            if (drop > 0) {
                dropBufferedBytes(drop);
                m_counters.bytesDropped += quint64(drop);
            }
            return std::nullopt;
        }
        if (sof > m_bufferOffset) {
            const qsizetype drop = sof - m_bufferOffset;
            dropBufferedBytes(drop);
            m_counters.bytesDropped += quint64(drop);
        }
        if (bufferedBytes() < kTypedTransportFrameOverhead) return std::nullopt;

        const auto* p = reinterpret_cast<const quint8*>(m_buffer.constData() + m_bufferOffset);
        const quint16 payloadLength = typedReadU16Le(p + 7);
        if (payloadLength > kTypedTransportMaxPayloadLength) {
            ++m_counters.lengthFailures;
            dropBufferedBytes(1);
            ++m_counters.bytesDropped;
            continue;
        }

        const qsizetype frameLength = kTypedTransportFrameOverhead + qsizetype(payloadLength);
        if (bufferedBytes() < frameLength) return std::nullopt;

        const quint16 expectedCrc = typedReadU16Le(p + 9 + payloadLength);
        const quint16 actualCrc = crc16Ccitt(p + 2, kTypedTransportHeaderSize + payloadLength);
        if (expectedCrc != actualCrc) {
            ++m_counters.crcFailures;
            dropBufferedBytes(1);
            ++m_counters.bytesDropped;
            continue;
        }

        TypedRecord record;
        record.header.version = p[2];
        record.header.recordType = p[3];
        record.header.flags = p[4];
        record.header.seq = typedReadU16Le(p + 5);
        record.header.payloadLength = payloadLength;
        record.payload = QByteArray(reinterpret_cast<const char*>(p + 9), payloadLength);
        record.frameBytes = QByteArray(reinterpret_cast<const char*>(p), frameLength);

        if (record.header.version != kTypedTransportVersion) {
            ++m_counters.versionWarnings;
        }
        noteSequence(record.header.seq);
        ++m_counters.frames;

        dropBufferedBytes(frameLength);
        return record;
    }
}
