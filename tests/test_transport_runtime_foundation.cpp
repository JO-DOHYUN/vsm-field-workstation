#include "transport/HostTxQueue.h"
#include "transport/HostTxRuntime.h"
#include "transport/LiveProjectionRuntime.h"
#include "transport/LiveTruthRuntime.h"
#include "transport/TransportSession.h"
#include "transport/TransportRuntime.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <QJsonObject>

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

    void liveTruthRuntimeConsumesAllCanRxAndSnapshotsLatestPerBusKey() {
        CanMonitorTransport::LiveTruthRuntime runtime;

        TypedRecordList records;
        records << makeCanRxRecord(1, 0x120, 1000, 0);
        records << makeCanRxRecord(2, 0x120, 1200, 0);
        records << makeCanRxRecord(3, 0x120, 1100, 1);
        records << makeCanRxRecord(4, 0x120, 2000, 0);

        const auto result = runtime.ingest(records);

        QCOMPARE(result.frames.size(), 2);
        QCOMPARE(result.status.observedCanRxFrames, quint64(4));
        QCOMPARE(result.status.observedBus0CanRxFrames, quint64(3));
        QCOMPARE(result.status.observedBus1CanRxFrames, quint64(1));
        QCOMPARE(result.status.emittedTruthFrames, quint64(2));
        QCOMPARE(result.status.coalescedTruthUpdates, quint64(2));
        QCOMPARE(result.status.truthLoss, quint64(0));

        const auto bus0It = std::find_if(result.frames.cbegin(), result.frames.cend(), [](const FrameRecord& frame) {
            return frame.bus == 0 && frame.canId == 0x120;
        });
        const auto bus1It = std::find_if(result.frames.cbegin(), result.frames.cend(), [](const FrameRecord& frame) {
            return frame.bus == 1 && frame.canId == 0x120;
        });
        QVERIFY(bus0It != result.frames.cend());
        QVERIFY(bus1It != result.frames.cend());
        QCOMPARE(bus0It->tExtUs, quint64(2000));
        QVERIFY(bus0It->hasObservedGap);
        QCOMPARE(bus0It->observedGapUs, quint64(800));
        QCOMPARE(bus1It->tExtUs, quint64(1100));
        QVERIFY(!bus1It->hasObservedGap);
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
        CanMonitorTransport::TransportSession::BoardUplinkCounters uplink;
        uplink.present = true;
        uplink.serialBackpressureTotal = 2;
        uplink.serialRingClearTotal = 0;
        uplink.serialTxHighWaterBytes = 4096;
        uplink.hasPoolCounters = true;
        uplink.uplinkLargePoolUsedBlocks = 9;
        uplink.uplinkLargePoolCapacityBlocks = 40;
        uplink.uplinkLargePoolCanReserveUsedBlocks = 2;
        uplink.canTruthDescriptorQueueHighWater = 11;
        uplink.uplinkDescriptorHighWaterTotal = 14;
        uplink.diagnosticSuppressedTotal = 15;
        session.updateBoardHealth(0, 0, 100, uplink);
        session.updateLiveRuntime(8000, 7990, 7900, 17, 5, 100, 12, 0, 80, 25, 55, 256, 3, 4);
        session.updateLiveTruth(100, 80, 20, 50, 50, 3, 2, 4, 30, 2, 1, 0);
        session.updateRawLedger(100, 80, 4096, 0, 99);
        session.updateDrainPipeline(8192, 4, 100, 4096, 0, 2048, 16 * 1024 * 1024, 0, 0, 0, 1, 0, 1024, 0, 2);
        session.updateAnalysisQueue(0, 12, 131072, 100, 100, 0, 3, 1, 2, 0);
        QJsonObject liveTrace;
        liveTrace.insert(QStringLiteral("parsed_can_rx"), QStringLiteral("100"));
        liveTrace.insert(QStringLiteral("snapshot_emitted"), QStringLiteral("3"));
        liveTrace.insert(QStringLiteral("snapshot_emitted_frames"), QStringLiteral("12"));
        liveTrace.insert(QStringLiteral("snapshot_ack"), QStringLiteral("3"));
        liveTrace.insert(QStringLiteral("frames_received_emit"), QStringLiteral("3"));
        liveTrace.insert(QStringLiteral("frames_received_frames"), QStringLiteral("12"));
        liveTrace.insert(QStringLiteral("framesReceived_calls"), QStringLiteral("3"));
        liveTrace.insert(QStringLiteral("pending_live_rows"), QStringLiteral("0"));
        liveTrace.insert(QStringLiteral("append_live_batch_frames"), QStringLiteral("4"));
        liveTrace.insert(QStringLiteral("live_model_rows"), QStringLiteral("4"));
        session.updateLivePathTrace(liveTrace);
        QJsonObject drainTrace;
        drainTrace.insert(QStringLiteral("readyRead_per_sec"), 120.0);
        drainTrace.insert(QStringLiteral("bytes_available_emit_per_sec"), 60.0);
        drainTrace.insert(QStringLiteral("scheduleDrainPump_per_sec"), 60.0);
        drainTrace.insert(QStringLiteral("pump_per_sec"), 30.0);
        drainTrace.insert(QStringLiteral("output_signal_per_sec"), 10.0);
        drainTrace.insert(QStringLiteral("readyRead_calls"), QStringLiteral("120"));
        drainTrace.insert(QStringLiteral("pump_calls"), QStringLiteral("30"));
        drainTrace.insert(QStringLiteral("drain_queue_overrun_bytes"), QStringLiteral("0"));
        session.updateDrainEventTrace(drainTrace);
        const QVariantList rows = session.rows();
        QCOMPARE(rows.size(), 15);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("capture_storage"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("typed_parser"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("ERR"));
        QCOMPARE(rows.at(2).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("host_drain"));
        QVERIFY(rows.at(2).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("ready_max_us 100")));
        QCOMPARE(rows.at(3).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("capture_writer"));
        QCOMPARE(rows.at(4).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("analysis_queue"));
        QVERIFY(rows.at(4).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("processed 100")));
        QCOMPARE(rows.at(5).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("host_tx_queue"));
        QCOMPARE(rows.at(5).toMap().value(QStringLiteral("blocking")).toBool(), true);
        QCOMPARE(rows.at(6).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("board_health"));
        QCOMPARE(rows.at(7).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("csm_uplink"));
        QCOMPARE(rows.at(7).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("WARN"));
        QVERIFY(rows.at(7).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("large_pool 9/40")));
        QVERIFY(rows.at(7).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("can_q_high 11")));

        uplink.serialBackpressureTotal = 0;
        uplink.serialEnqueueFailTotal = 24;
        session.updateTypedStatus(10, 0, 0, 0, 0, 0);
        session.updateHostTxQueue(0, 0, 5, 2, 0);
        session.updateBoardHealth(0, 0, 100, uplink);
        QCOMPARE(session.level(), QStringLiteral("WARN"));
        QCOMPARE(session.rows().at(7).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("WARN"));
        QCOMPARE(session.rows().at(7).toMap().value(QStringLiteral("blocking")).toBool(), false);

        uplink.canTruthPoolAllocFailTotal = 1;
        session.updateBoardHealth(0, 0, 100, uplink);
        QCOMPARE(session.level(), QStringLiteral("ERR"));
        QCOMPARE(session.rows().at(7).toMap().value(QStringLiteral("blocking")).toBool(), true);
        uplink.canTruthPoolAllocFailTotal = 0;

        uplink.serialRingClearTotal = 1;
        session.updateBoardHealth(0, 0, 100, uplink);
        QCOMPARE(session.level(), QStringLiteral("ERR"));
        QCOMPARE(session.rows().at(7).toMap().value(QStringLiteral("blocking")).toBool(), true);

        QCOMPARE(rows.at(8).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("board_events"));
        QCOMPARE(rows.at(8).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("OK"));
        session.noteBoardEvent(9, 0x0618, 13, 123456);
        const QVariantList eventRows = session.rows();
        QCOMPARE(eventRows.at(8).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("WARN"));
        QVERIFY(eventRows.at(8).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("MCP2515 1")));
        QVERIFY(eventRows.at(8).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("0X0618:1")));
        QCOMPARE(rows.at(9).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_truth"));
        QVERIFY(rows.at(9).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("truth_loss 0")));
        QCOMPARE(rows.at(10).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("raw_ledger"));
        QVERIFY(rows.at(10).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("segment_bytes 4096")));
        QCOMPARE(rows.at(11).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_projection"));
        QVERIFY(rows.at(11).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("budget_hits 3")));
        QCOMPARE(rows.at(12).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_delay"));
        QCOMPARE(rows.at(13).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_path_trace"));
        QVERIFY(rows.at(13).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("parsed 100")));
        QCOMPARE(rows.at(14).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("drain_event_trace"));
        QVERIFY(rows.at(14).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("readyRead/s 120")));
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
