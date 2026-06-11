#include "PacketParser.h"

#include <algorithm>

quint8 PacketParser::crc8Atm(const quint8* data, int len) {
    quint8 crc = 0x00;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x80) crc = quint8((crc << 1) ^ 0x07);
            else crc = quint8(crc << 1);
        }
    }
    return crc;
}

bool PacketParser::isPlausible(const QByteArray& buf, int off) {
    if (buf.size() < off + 20) return false;
    const auto* p = reinterpret_cast<const quint8*>(buf.constData() + off);
    if (crc8Atm(p, 19) != p[19]) return false;
    const quint8 dlc = p[8] & 0x0F;
    if ((p[17] & 0x80) == 0 && dlc > 8) return false;
    return true;
}

void PacketParser::reset() {
    m_buffer.clear();
    m_haveLastTus = false;
    m_lastTus = 0;
    m_wrapCount = 0;
}

void PacketParser::append(const QByteArray& chunk) {
    m_buffer.append(chunk);
}

quint64 PacketParser::extendTus(quint32 tUs) {
    if (!m_haveLastTus) {
        m_haveLastTus = true;
        m_lastTus = tUs;
        return tUs;
    }
    const quint32 prevLow = quint32(m_lastTus & 0xFFFFFFFFULL);
    if (tUs < prevLow && (prevLow - tUs) > 0x80000000u) {
        ++m_wrapCount;
    }
    m_lastTus = (m_wrapCount << 32) | tUs;
    return m_lastTus;
}

std::optional<PacketRecord> PacketParser::decode20(const QByteArray& rec20, quint64& lastTus, bool& haveLastTus, quint64& wrapCount) {
    if (rec20.size() != 20) return std::nullopt;
    const auto* p = reinterpret_cast<const quint8*>(rec20.constData());
    if (crc8Atm(p, 19) != p[19]) return std::nullopt;

    const quint32 tUs = rdU32Le(p + 0);
    if (!haveLastTus) {
        haveLastTus = true;
        lastTus = tUs;
    }
    else {
        const quint32 prevLow = quint32(lastTus & 0xFFFFFFFFULL);
        if (tUs < prevLow && (prevLow - tUs) > 0x80000000u) ++wrapCount;
        lastTus = (wrapCount << 32) | tUs;
    }

    PacketRecord out;
    const quint8 busType = p[17];
    out.isStats = (busType & 0x80) != 0;
    if (!out.isStats) {
        out.frame.tExtUs = lastTus;
        const quint32 canIdFlags = rdU32Le(p + 4);
        out.frame.canId = (canIdFlags & 0x1FFFFFFF);
        out.frame.ext = ((canIdFlags >> 29) & 1) != 0;
        out.frame.rtr = ((canIdFlags >> 30) & 1) != 0;
        out.frame.dlc = p[8] & 0x0F;
        std::memcpy(out.frame.data, p + 9, 8);
        out.frame.bus = quint8(busType & 0x03);
        out.frame.seq = p[18];
        out.frame.raw20 = rec20;
    }
    else {
        out.stats.tExtUs = lastTus;
        out.stats.droppedTotal = rdU32Le(p + 4);
        out.stats.fifoOverflowTotal = rdU32Le(p + 8);
        out.stats.rxFps1s = quint16(p[12]) | (quint16(p[13]) << 8);
        out.stats.txFps1s = quint16(p[14]) | (quint16(p[15]) << 8);
        out.stats.errPassive1s = p[16];
        out.stats.busOff1s = quint8(p[17] & 0x7F);
        out.stats.seq = p[18];
        out.stats.raw20 = rec20;
    }
    return out;
}

std::optional<PacketRecord> PacketParser::takeOne() {
    while (true) {
        if (m_buffer.size() < 20) return std::nullopt;
        if (!isPlausible(m_buffer, 0)) {
            const int scanMax = std::min<int>(int(m_buffer.size()) - 20, 512);
            int found = -1;
            for (int i = 1; i <= scanMax; ++i) {
                if (isPlausible(m_buffer, i)) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                const qsizetype drop = qsizetype(std::max<int>(0, int(m_buffer.size()) - 19));
                m_buffer.remove(0, drop);
                return std::nullopt;
            }
            m_buffer.remove(0, found);
            if (m_buffer.size() < 20) return std::nullopt;
        }

        QByteArray rec20 = m_buffer.left(20);
        m_buffer.remove(0, 20);
        auto decoded = decode20(rec20, m_lastTus, m_haveLastTus, m_wrapCount);
        if (decoded) return decoded;
    }
}
