#pragma once

#include "CanTypes.h"
#include <QByteArray>
#include <optional>

class PacketParser {
public:
    void reset();
    void append(const QByteArray& chunk);
    std::optional<PacketRecord> takeOne();
    static std::optional<PacketRecord> decode20(const QByteArray& rec20, quint64& lastTus, bool& haveLastTus, quint64& wrapCount);

private:
    static quint8 crc8Atm(const quint8* data, int len);
    static bool isPlausible(const QByteArray& buf, int off);
    quint64 extendTus(quint32 tUs);

    QByteArray m_buffer;
    bool m_haveLastTus = false;
    quint64 m_lastTus = 0;
    quint64 m_wrapCount = 0;
};
