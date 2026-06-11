#include "ReplayEngine.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cstring>

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

QString writeReplayFixture(const QVector<QByteArray>& records) {
    static int counter = 0;
    QTemporaryDir dir;
    dir.setAutoRemove(false);
    const QString path = dir.path() + QStringLiteral("/replay_%1.bin").arg(++counter);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    for (const QByteArray& record : records) file.write(record);
    file.close();
    return path;
}

} // namespace

class ReplayEngineTest : public QObject {
    Q_OBJECT

private slots:
    void loadsReplayAndPublishesCursor() {
        const QString path = writeReplayFixture({
            framePacket(1000, 0x101, 0, 1, QByteArray::fromHex("0102030405060708")),
            framePacket(1600, 0x101, 0, 2, QByteArray::fromHex("1112131415161718")),
            framePacket(2500, 0x101, 0, 3, QByteArray::fromHex("2122232425262728"))
        });
        QVERIFY(!path.isEmpty());

        ReplayEngine engine;
        QSignalSpy loadedSpy(&engine, &ReplayEngine::replayLoaded);
        QSignalSpy cursorSpy(&engine, &ReplayEngine::replayCursorChanged);

        QVERIFY(engine.loadFile(path));
        QCOMPARE(loadedSpy.count(), 1);
        QCOMPARE(loadedSpy.takeFirst().at(0).toBool(), true);
        QVERIFY(cursorSpy.count() >= 1);

        QCOMPARE(engine.frameCount(), 3);
        QCOMPARE(engine.currentIndex(), 0);
        QCOMPARE(engine.currentUs(), quint64(1000));
        QCOMPARE(engine.durationUs(), quint64(1500));
        QCOMPARE(engine.progress(), 0.0);
    }

    void legacyReplayPreservesVariableDlc() {
        const QString path = writeReplayFixture({
            framePacket(1000, 0x111, 0, 1, QByteArray::fromHex("AABB")),
            framePacket(2000, 0x112, 0, 2, QByteArray::fromHex("0102030405"))
        });
        QVERIFY(!path.isEmpty());

        ReplayEngine engine;
        QVERIFY(engine.loadFile(path));
        QCOMPARE(engine.frameCount(), 2);
        QCOMPARE(engine.frames().at(0).dlc, quint8(2));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(engine.frames().at(0).data), 2), QByteArray::fromHex("AABB"));
        QCOMPARE(engine.frames().at(1).dlc, quint8(5));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(engine.frames().at(1).data), 5), QByteArray::fromHex("0102030405"));
    }

    void loadsDecodedFramesForTypedReplayProjection() {
        FrameRecord first;
        first.tExtUs = 3000;
        first.canId = 0x321;
        first.dlc = 8;
        first.bus = 1;
        first.seq = 3;
        std::memset(first.data, 0x11, sizeof(first.data));

        FrameRecord second;
        second.tExtUs = 1000;
        second.canId = 0x530;
        second.dlc = 8;
        second.bus = 0;
        second.seq = 1;
        std::memset(second.data, 0x22, sizeof(second.data));

        ReplayEngine engine;
        QSignalSpy loadedSpy(&engine, &ReplayEngine::replayLoaded);
        QVERIFY(engine.loadFrames(QStringLiteral("typed_fixture"), {first, second}));

        QCOMPARE(loadedSpy.count(), 1);
        QCOMPARE(loadedSpy.takeFirst().at(0).toBool(), true);
        QCOMPARE(engine.frameCount(), 2);
        QCOMPARE(engine.currentUs(), quint64(1000));
        QCOMPARE(engine.durationUs(), quint64(2000));
        QCOMPARE(engine.frames().front().canId, quint32(0x530));
        QCOMPARE(engine.frames().back().canId, quint32(0x321));
    }

    void seekAndStepStayClamped() {
        const QString path = writeReplayFixture({
            framePacket(1000, 0x121, 0, 1, QByteArray::fromHex("0100000000000000")),
            framePacket(2000, 0x122, 0, 2, QByteArray::fromHex("0200000000000000")),
            framePacket(3000, 0x123, 0, 3, QByteArray::fromHex("0300000000000000"))
        });
        QVERIFY(!path.isEmpty());

        ReplayEngine engine;
        QVERIFY(engine.loadFile(path));

        engine.seekToRatio(0.5);
        QCOMPARE(engine.currentIndex(), 1);
        QCOMPARE(engine.currentUs(), quint64(2000));

        QSignalSpy frameSpy(&engine, &ReplayEngine::replayFrame);
        engine.stepFrames(1);
        QCOMPARE(frameSpy.count(), 1);
        const FrameRecord stepped = qvariant_cast<FrameRecord>(frameSpy.takeFirst().at(0));
        QCOMPARE(stepped.canId, quint32(0x123));
        QCOMPARE(engine.currentIndex(), 2);
        QCOMPARE(engine.currentUs(), quint64(3000));

        engine.seekToRatio(4.0);
        QCOMPARE(engine.currentIndex(), 2);
        engine.stepFrames(10);
        QCOMPARE(engine.currentIndex(), 2);
    }

    void playEmitsFrameAndFinishesForSingleFrameReplay() {
        const QString path = writeReplayFixture({
            framePacket(1000, 0x201, 0, 1, QByteArray::fromHex("0100000000000000"))
        });
        QVERIFY(!path.isEmpty());

        ReplayEngine engine;
        QVERIFY(engine.loadFile(path));

        QSignalSpy frameSpy(&engine, &ReplayEngine::replayFrame);
        QSignalSpy finishedSpy(&engine, &ReplayEngine::replayFinished);
        QSignalSpy loopSpy(&engine, &ReplayEngine::replayLoopChanged);

        engine.setLoop(true);
        QCOMPARE(loopSpy.count(), 1);
        QCOMPARE(engine.loopEnabled(), true);

        engine.setLoop(false);
        QCOMPARE(loopSpy.count(), 2);
        QCOMPARE(engine.loopEnabled(), false);

        engine.play(0.0);

        QCOMPARE(frameSpy.count(), 1);
        QCOMPARE(finishedSpy.count(), 1);
        QCOMPARE(engine.currentIndex(), 1);
        QCOMPARE(engine.speed(), 1.0);
    }
};

QTEST_APPLESS_MAIN(ReplayEngineTest)

#include "test_replay_engine.moc"
