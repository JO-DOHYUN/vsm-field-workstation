#include "transport/HostTxQueue.h"
#include "transport/HostTxRuntime.h"
#include "transport/LiveProjectionRuntime.h"
#include "transport/TransportSession.h"
#include "transport/TransportRuntime.h"

#include <QtTest/QtTest>

namespace {
void appendU32(QByteArray& out, quint32 value) {
    for (int byte = 0; byte < 4; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

void appendU64(QByteArray& out, quint64 value) {
    for (int byte = 0; byte < 8; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

TypedRecord makeCanRxRecord(quint16 seq, quint32 canId, quint64 monoUs, quint8 bus) {
    QByteArray payload;
    appendU64(payload, monoUs);
    appendU32(payload, canId);
    payload.append(char(8));
    payload.append(char(bus));
    payload.append(QByteArray::fromHex("1122334455667788"));
    appendU32(payload, seq);
    appendU32(payload, 0);

    TypedRecord record;
    record.header.version = kTypedTransportVersion;
    record.header.recordType = static_cast<quint8>(TypedRecordType::CanRxRaw);
    record.header.seq = seq;
    record.header.payloadLength = quint16(payload.size());
    record.payload = payload;
    return record;
}

TypedRecord makeCanTxRecord(quint16 seq, quint32 canId, quint64 monoUs, quint8 bus) {
    TypedRecord record = makeCanRxRecord(seq, canId, monoUs, bus);
    record.header.recordType = static_cast<quint8>(TypedRecordType::CanTxRaw);
    return record;
}

TypedRecord makeControlAckRecord(quint16 seq, quint32 commandId, quint32 canId, quint64 monoUs, quint8 bus, quint8 status = 1) {
    QByteArray payload;
    appendU64(payload, monoUs);
    appendU32(payload, commandId);
    payload.append(char(status));
    payload.append(char(0));
    payload.append(char(bus));
    payload.append(char(8));
    appendU32(payload, canId);
    appendU32(payload, seq);
    appendU32(payload, 0);

    TypedRecord record;
    record.header.version = kTypedTransportVersion;
    record.header.recordType = static_cast<quint8>(TypedRecordType::ControlAck);
    record.header.seq = seq;
    record.header.payloadLength = quint16(payload.size());
    record.payload = payload;
    return record;
}

TypedRecord makeCriticalRecord(TypedRecordType type, quint16 seq) {
    TypedRecord record;
    record.header.version = kTypedTransportVersion;
    record.header.recordType = static_cast<quint8>(type);
    record.header.seq = seq;
    record.payload = QByteArray(kTypedBoardEventPayloadSize, char(0));
    record.header.payloadLength = quint16(record.payload.size());
    return record;
}
}

class TransportRuntimeFoundationTest : public QObject {
    Q_OBJECT

private slots:
    void hostTxQueuePreservesFifoAndCounters() {
        CanMonitorTransport::HostTxQueue queue(4, 128);

        QString error;
        QVERIFY(queue.enqueue(QByteArray::fromHex("A55A010A00010000AA00"), QStringLiteral("first"), &error));
        QVERIFY(error.isEmpty());
        QVERIFY(queue.enqueue(QByteArray::fromHex("A55A010A00010000BB00"), QStringLiteral("second"), &error));

        QCOMPARE(queue.queuedFrames(), qsizetype(2));
        QCOMPARE(queue.enqueuedFrames(), quint64(2));
        QCOMPARE(queue.writtenFrames(), quint64(0));
        QCOMPARE(queue.droppedFrames(), quint64(0));

        const auto first = queue.dequeue();
        QCOMPARE(first.sequence, quint64(1));
        QCOMPARE(first.summary, QStringLiteral("first"));
        queue.markWritten();

        const auto second = queue.dequeue();
        QCOMPARE(second.sequence, quint64(2));
        QCOMPARE(second.summary, QStringLiteral("second"));
        queue.markWritten();

        QVERIFY(!queue.hasPending());
        QCOMPARE(queue.writtenFrames(), quint64(2));
    }

    void hostTxQueueRejectsOverflowWithoutDroppingQueuedItems() {
        CanMonitorTransport::HostTxQueue queue(1, 16);

        QString error;
        QVERIFY(queue.enqueue(QByteArray(12, char(0x11)), QStringLiteral("first"), &error));
        QVERIFY(!queue.enqueue(QByteArray(4, char(0x22)), QStringLiteral("second"), &error));
        QVERIFY(error.contains(QStringLiteral("full")));
        QCOMPARE(queue.queuedFrames(), qsizetype(1));
        QCOMPARE(queue.droppedFrames(), quint64(1));

        const auto first = queue.dequeue();
        QCOMPARE(first.summary, QStringLiteral("first"));
    }

    void hostTxQueueRejectsByteLimit() {
        CanMonitorTransport::HostTxQueue queue(4, 8);

        QString error;
        QVERIFY(!queue.enqueue(QByteArray(9, char(0x33)), QStringLiteral("large"), &error));
        QVERIFY(error.contains(QStringLiteral("byte")));
        QCOMPARE(queue.queuedFrames(), qsizetype(0));
        QCOMPARE(queue.droppedFrames(), quint64(1));
    }

    void hostTxRuntimeOwnsBackpressureStatusAndClearReason() {
        CanMonitorTransport::HostTxRuntime runtime(8);

        auto first = runtime.enqueue(QByteArray(6, char(0x11)), QStringLiteral("first"));
        QVERIFY(first.ok);
        QCOMPARE(first.status.queuedFrames, quint64(1));
        QCOMPARE(first.status.enqueuedFrames, quint64(1));

        auto blocked = runtime.takeNextForWrite(8);
        QVERIFY(!blocked.has_value());

        auto item = runtime.takeNextForWrite(0);
        QVERIFY(item.has_value());
        QCOMPARE(item->summary, QStringLiteral("first"));
        runtime.markWritten();
        QCOMPARE(runtime.status().writtenFrames, quint64(1));

        QVERIFY(runtime.enqueue(QByteArray(4, char(0x22)), QStringLiteral("pending")).ok);
        const auto cleared = runtime.clear(QStringLiteral("disconnect"));
        QVERIFY(cleared.hadPending);
        QVERIFY(cleared.error.contains(QStringLiteral("disconnect")));
        QCOMPARE(cleared.status.queuedFrames, quint64(0));
    }

    void liveProjectionRuntimeCoalescesCanRxAndKeepsCriticalEvidence() {
        CanMonitorTransport::LiveProjectionRuntime runtime(2);

        TypedRecordList records;
        records << makeCanRxRecord(1, 0x120, 1000, 0);
        records << makeCanRxRecord(2, 0x120, 2000, 0);
        records << makeCanRxRecord(3, 0x121, 3000, 1);
        records << makeCanRxRecord(4, 0x122, 4000, 1);
        records << makeCriticalRecord(TypedRecordType::BoardEvent, 5);

        const auto result = runtime.ingest(records);
        QCOMPARE(result.projectedFrames.size(), 2);
        QCOMPARE(result.projectedFrames.at(0).canId, quint32(0x121));
        QCOMPARE(result.projectedFrames.at(1).canId, quint32(0x122));
        QCOMPARE(result.criticalRecords.size(), 1);
        QCOMPARE(result.criticalRecords.first().header.recordType, static_cast<quint8>(TypedRecordType::BoardEvent));
        QCOMPARE(result.status.observedCanRxFrames, quint64(4));
        QCOMPARE(result.status.projectedCanRxFrames, quint64(2));
        QCOMPARE(result.status.sampledCanRxFrames, quint64(2));
        QCOMPARE(result.status.workerDroppedCanRxFrames, quint64(1));
        QCOMPARE(result.status.observedBus0CanRxFrames, quint64(2));
        QCOMPARE(result.status.observedBus1CanRxFrames, quint64(2));
    }

    void liveProjectionRuntimeSamplesRoutineControlEvidence() {
        CanMonitorTransport::LiveProjectionRuntime runtime(8);

        TypedRecordList records;
        records << makeControlAckRecord(1, 0x9001, 0x510, 1000, 0);
        records << makeControlAckRecord(2, 0x9002, 0x510, 1100, 0);
        records << makeCanTxRecord(3, 0x510, 1200, 0);
        records << makeCanTxRecord(4, 0x510, 1300, 0);
        records << makeCanRxRecord(5, 0x510, 1400, 0);
        records << makeCanRxRecord(6, 0x510, 1500, 0);

        const auto result = runtime.ingest(records);

        QCOMPARE(result.criticalRecords.size(), 3);
        QCOMPARE(result.criticalRecords.at(0).header.recordType, static_cast<quint8>(TypedRecordType::ControlAck));
        QCOMPARE(result.criticalRecords.at(0).header.seq, quint16(2));
        QCOMPARE(result.criticalRecords.at(1).header.recordType, static_cast<quint8>(TypedRecordType::CanTxRaw));
        QCOMPARE(result.criticalRecords.at(1).header.seq, quint16(4));
        QCOMPARE(result.criticalRecords.at(2).header.recordType, static_cast<quint8>(TypedRecordType::CanRxRaw));
        QCOMPARE(result.criticalRecords.at(2).header.seq, quint16(6));
        QCOMPARE(result.status.observedControlEvidenceRecords, quint64(6));
        QCOMPARE(result.status.projectedControlEvidenceRecords, quint64(3));
        QCOMPARE(result.status.sampledControlEvidenceRecords, quint64(3));
    }

    void transportRuntimeNormalizesProductionModeKeys() {
        QCOMPARE(CanMonitorTransport::TransportRuntime::normalizeModeKey(QStringLiteral("typed-evidence")),
                 QStringLiteral("typed"));
        QCOMPARE(CanMonitorTransport::TransportRuntime::normalizeModeKey(QStringLiteral("typed_evidence")),
                 QStringLiteral("typed"));
        QCOMPARE(CanMonitorTransport::TransportRuntime::normalizeModeKey(QStringLiteral("legacy")),
                 QStringLiteral("legacy20"));
        QCOMPARE(CanMonitorTransport::TransportRuntime::modeForKey(QStringLiteral("typed")),
                 SerialWorker::TransportMode::TypedEvidence);
        QCOMPARE(CanMonitorTransport::TransportRuntime::modeForKey(QStringLiteral("legacy20")),
                 SerialWorker::TransportMode::Legacy20);
    }

    void transportSessionSummarizesParserQueueAndLiveDelay() {
        CanMonitorTransport::TransportSession session;

        QCOMPARE(session.level(), QStringLiteral("OK"));
        QVERIFY(session.summary().contains(QStringLiteral("transport OK")));

        session.setConnected(true);
        session.updateLiveRuntime(5000, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        QCOMPARE(session.level(), QStringLiteral("WARN"));
        QVERIFY(session.summary().contains(QStringLiteral("no recent live frame")));

        session.updateTypedStatus(10, 0, 1, 0, 0, 2);
        QCOMPARE(session.level(), QStringLiteral("ERR"));
        QCOMPARE(session.parserFaultCount(), quint64(3));

        session.updateHostTxQueue(3, 120, 5, 2, 1);
        QCOMPARE(session.hostBackpressureCount(), quint64(1));
        session.updateCaptureStorage(true, 2048, 31);
        session.updateBoardHealth(0, 0, 100);
        session.updateLiveRuntime(8000, 7990, 7900, 17, 5, 100, 12, 0, 80, 25, 55, 256, 3, 4);
        const QVariantList rows = session.rows();
        QCOMPARE(rows.size(), 6);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("capture_storage"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("typed_parser"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("ERR"));
        QCOMPARE(rows.at(2).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("host_tx_queue"));
        QCOMPARE(rows.at(2).toMap().value(QStringLiteral("blocking")).toBool(), true);
        QCOMPARE(rows.at(3).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("board_health"));
        QCOMPARE(rows.at(4).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_projection"));
        QVERIFY(rows.at(4).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("budget_hits 3")));
        QCOMPARE(rows.at(5).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_delay"));
    }

    void transportRuntimeOwnsWorkerThreadAndQueuesModeChanges() {
        CanMonitorTransport::TransportRuntime runtime;
        SerialWorker* worker = runtime.createWorker();
        QVERIFY(worker != nullptr);
        QVERIFY(runtime.hasWorker());
        QVERIFY(runtime.ownsWorker());
        QCOMPARE(worker->transportMode(), SerialWorker::TransportMode::Legacy20);

        runtime.startWorkerThread();
        QTRY_VERIFY(runtime.workerThreadRunning());

        QString error;
        QVERIFY2(runtime.setTransportModeKey(QStringLiteral("typed"), &error), qPrintable(error));
        QTRY_COMPARE(worker->transportMode(), SerialWorker::TransportMode::TypedEvidence);

        runtime.shutdown();
        QVERIFY(!runtime.hasWorker());
        QVERIFY(!runtime.workerThreadRunning());
    }
};

QTEST_MAIN(TransportRuntimeFoundationTest)

#include "test_transport_runtime_foundation.moc"
