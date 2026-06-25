#include "transport/DrainByteQueue.h"

#include <QtTest>

class DrainByteQueueTest : public QObject {
    Q_OBJECT

private slots:
    void preservesOrderAndCounters() {
        CanMonitorTransport::DrainByteQueue queue(64);
        QVERIFY(queue.push(QByteArray("abc")));
        QVERIFY(queue.push(QByteArray("defg")));

        const auto blocks = queue.popAll();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks.at(0).bytes, QByteArray("abc"));
        QCOMPARE(blocks.at(1).bytes, QByteArray("defg"));
        QCOMPARE(blocks.at(0).sequence, quint64(1));
        QCOMPARE(blocks.at(1).sequence, quint64(2));

        const auto snapshot = queue.snapshot();
        QCOMPARE(snapshot.usedBytes, quint64(0));
        QCOMPARE(snapshot.maxUsedBytes, quint64(7));
        QCOMPARE(snapshot.pushedBlocks, quint64(2));
        QCOMPARE(snapshot.poppedBlocks, quint64(2));
        QCOMPARE(snapshot.overrunBytes, quint64(0));
    }

    void rejectsOverflowWithoutDroppingExistingBlocks() {
        CanMonitorTransport::DrainByteQueue queue(8);
        QVERIFY(queue.push(QByteArray("123456")));
        QVERIFY(!queue.push(QByteArray("789")));

        auto snapshot = queue.snapshot();
        QCOMPARE(snapshot.usedBytes, quint64(6));
        QCOMPARE(snapshot.overrunBytes, quint64(3));

        const auto blocks = queue.popAll();
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.first().bytes, QByteArray("123456"));
    }

    void popAllCanLimitBatchBytes() {
        CanMonitorTransport::DrainByteQueue queue(64);
        QVERIFY(queue.push(QByteArray("1111")));
        QVERIFY(queue.push(QByteArray("2222")));
        QVERIFY(queue.push(QByteArray("3333")));

        const auto first = queue.popAll(8);
        QCOMPARE(first.size(), 2);
        QCOMPARE(queue.snapshot().usedBytes, quint64(4));

        const auto second = queue.popAll();
        QCOMPARE(second.size(), 1);
        QCOMPARE(second.first().bytes, QByteArray("3333"));
    }
};

QTEST_APPLESS_MAIN(DrainByteQueueTest)

#include "test_drain_byte_queue.moc"
