#pragma once

#include "TypedRecords.h"

#include <QByteArray>
#include <optional>

class TypedTransportParser {
public:
    struct Counters {
        quint64 frames = 0;
        quint64 bytesDropped = 0;
        quint64 crcFailures = 0;
        quint64 lengthFailures = 0;
        quint64 versionWarnings = 0;
        quint64 seqGaps = 0;
    };

    void reset();
    void append(const QByteArray& chunk);
    std::optional<TypedRecord> takeOne();

    const Counters& counters() const { return m_counters; }
    qsizetype bufferedBytes() const { return m_buffer.size() - m_bufferOffset; }

    static quint16 crc16Ccitt(const quint8* data, qsizetype len);

private:
    static qsizetype findSof(const QByteArray& buffer, qsizetype offset);
    void dropBufferedBytes(qsizetype count);
    void compactBufferIfNeeded(bool force = false);
    bool noteSequence(quint16 seq);

    QByteArray m_buffer;
    qsizetype m_bufferOffset = 0;
    Counters m_counters;
    bool m_haveLastSeq = false;
    quint16 m_lastSeq = 0;
};
