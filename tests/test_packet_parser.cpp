#include "PacketParser.h"

#include <QtTest/QtTest>

namespace {

quint8 crc8Atm(const QByteArray& bytes) {
    quint8 crc = 0x00;
    for (const char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) crc = quint8((crc << 1) ^ 0x07);
            else crc = quint8(crc << 1);
        }
    }
    return crc;
}

QByteArray framePacket(quint32 tus, quint32 canId, quint8 bus, quint8 seq, const QByteArray& payload) {
    QByteArray packet(20, Qt::Uninitialized);
    packet[0] = char(tus & 0xFF);
    packet[1] = char((tus >> 8) & 0xFF);
    packet[2] = char((tus >> 16) & 0xFF);
    packet[3] = char((tus >> 24) & 0xFF);
    packet[4] = char(canId & 0xFF);
    packet[5] = char((canId >> 8) & 0xFF);
    packet[6] = char((canId >> 16) & 0xFF);
    packet[7] = char((canId >> 24) & 0x1F);
    packet[8] = char(payload.size() & 0x0F);
    for (int index = 0; index < 8; ++index) {
        packet[9 + index] = index < payload.size() ? payload.at(index) : char(0);
    }
    packet[17] = char(bus & 0x03);
    packet[18] = char(seq);
    packet[19] = char(crc8Atm(packet.left(19)));
    return packet;
}

} // namespace

class PacketParserTest : public QObject {
    Q_OBJECT

private slots:
    void decodesValidFrame() {
        PacketParser parser;
        const QByteArray payload = QByteArray::fromHex("1122334455667788");
        parser.append(framePacket(1000, 0x123, 1, 7, payload));

        const auto record = parser.takeOne();
        QVERIFY(record.has_value());
        QVERIFY(!record->isStats);
        QCOMPARE(record->frame.tExtUs, quint64(1000));
        QCOMPARE(record->frame.canId, quint32(0x123));
        QCOMPARE(record->frame.bus, quint8(1));
        QCOMPARE(record->frame.seq, quint8(7));
        QCOMPARE(record->frame.dlc, quint8(8));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(record->frame.data), 8), payload);
    }

    void extendsTimestampAcrossWrap() {
        quint64 lastTus = 0;
        bool haveLastTus = false;
        quint64 wrapCount = 0;

        const QByteArray first = framePacket(0xFFFFFFF0u, 0x321, 0, 1, QByteArray::fromHex("0102030405060708"));
        const QByteArray second = framePacket(0x00000020u, 0x321, 0, 2, QByteArray::fromHex("1112131415161718"));

        const auto firstRecord = PacketParser::decode20(first, lastTus, haveLastTus, wrapCount);
        QVERIFY(firstRecord.has_value());
        QCOMPARE(firstRecord->frame.tExtUs, quint64(0xFFFFFFF0u));

        const auto secondRecord = PacketParser::decode20(second, lastTus, haveLastTus, wrapCount);
        QVERIFY(secondRecord.has_value());
        QCOMPARE(secondRecord->frame.tExtUs, quint64((1ULL << 32) | 0x20u));
    }

    void rejectsBadCrc() {
        PacketParser parser;
        QByteArray packet = framePacket(42, 0x77, 0, 3, QByteArray::fromHex("AAAAAAAAAAAAAAAA"));
        packet[19] = char(0x00);
        parser.append(packet);
        QVERIFY(!parser.takeOne().has_value());
    }
};

QTEST_APPLESS_MAIN(PacketParserTest)

#include "test_packet_parser.moc"
