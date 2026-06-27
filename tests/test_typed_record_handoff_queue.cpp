#include "transport/TypedRecordHandoffQueue.h"
#include "TypedRecords.h"

#include <QtTest/QtTest>

#include <algorithm>

using CanMonitorTransport::TypedRecordHandoffQueue;

namespace {
TypedCaptureFrame makeFrame(int bytes, quint16 seq = 1) {
    TypedCaptureFrame frame;
    frame.header.version = kTypedTransportVersion;
    frame.header.recordType = static_cast<quint8>(TypedRecordType::CanRxRaw);
    frame.header.seq = seq;
    frame.header.payloadLength = quint16(std::max(0, bytes - int(kTypedTransportFrameOverhead)));
    frame.frameBytes = QByteArray(bytes, '\x11');
    frame.monoUs = quint64(seq);
    return frame;
}
}

class TypedRecordHandoffQueueTest : public QObject {
    Q_OBJECT
private slots:
    void preservesOrderAndBoundsBytes() {
        TypedRecordHandoffQueue queue(128);
        TypedCaptureFrameList first;
        first.push_back(makeFrame(20, 1));
        first.push_back(makeFrame(30, 2));
        auto push = queue.push(std::move(first));
        QVERIFY(push.accepted);
        QVERIFY(push.shouldScheduleDrain);

        TypedCaptureFrameList second;
        second.push_back(makeFrame(40, 3));
        push = queue.push(std::move(second));
        QVERIFY(push.accepted);
        QVERIFY(!push.shouldScheduleDrain);

        auto snapshot = queue.snapshot();
        QCOMPARE(snapshot.queuedRecords, quint64(3));
        QCOMPARE(snapshot.queuedBytes, quint64(90));

        const TypedCaptureFrameList popped = queue.popFrames(3, 128);
        QCOMPARE(popped.size(), 3);
        QCOMPARE(popped.at(0).header.seq, quint16(1));
        QCOMPARE(popped.at(1).header.seq, quint16(2));
        QCOMPARE(popped.at(2).header.seq, quint16(3));
        QVERIFY(!queue.hasQueuedRecords());
        QVERIFY(!queue.snapshot().drainScheduled);
    }

    void rejectsOverCapacityWithoutSilentGrowth() {
        TypedRecordHandoffQueue queue(64);
        TypedCaptureFrameList first;
        first.push_back(makeFrame(48, 1));
        QVERIFY(queue.push(std::move(first)).accepted);

        TypedCaptureFrameList second;
        second.push_back(makeFrame(32, 2));
        const auto push = queue.push(std::move(second));
        QVERIFY(!push.accepted);
        QVERIFY(!push.error.isEmpty());

        const auto snapshot = queue.snapshot();
        QCOMPARE(snapshot.queuedRecords, quint64(1));
        QCOMPARE(snapshot.queuedBytes, quint64(48));
        QCOMPARE(snapshot.overrunRecords, quint64(1));
        QCOMPARE(snapshot.overrunBytes, quint64(32));
    }
};

QTEST_APPLESS_MAIN(TypedRecordHandoffQueueTest)

#include "test_typed_record_handoff_queue.moc"
