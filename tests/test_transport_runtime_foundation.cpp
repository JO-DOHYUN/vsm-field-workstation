#include "core/CoreViewTypes.h"
#include "transport/CaptureCoreRuntime.h"
#include "transport/HostTxQueue.h"
#include "transport/HostTxRuntime.h"
#include "transport/LiveProjectionRuntime.h"
#include "transport/LiveLatestRuntime.h"
#include "transport/TransportSession.h"
#include "transport/TransportRuntime.h"
#include "transport/TypedEvidencePipelineWorkerRuntime.h"
#include "TypedTransportParser.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <QJsonArray>
#include <QJsonObject>

namespace {
void appendU16(QByteArray& out, quint16 value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

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

QByteArray makeTypedFrame(const TypedRecord& record) {
    QByteArray frame;
    frame.reserve(int(kTypedTransportFrameOverhead + record.payload.size()));
    frame.append(char(kTypedTransportSof0));
    frame.append(char(kTypedTransportSof1));
    frame.append(char(record.header.version));
    frame.append(char(record.header.recordType));
    frame.append(char(record.header.flags));
    appendU16(frame, record.header.seq);
    appendU16(frame, quint16(record.payload.size()));
    frame.append(record.payload);
    const auto* crcStart = reinterpret_cast<const quint8*>(frame.constData() + 2);
    appendU16(frame, TypedTransportParser::crc16Ccitt(crcStart, frame.size() - 2));
    return frame;
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
    void initTestCase() {
        qRegisterMetaType<QJsonObject>("QJsonObject");
    }

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

        runtime.ingestRecord(makeCanRxRecord(1, 0x120, 1000, 0));
        runtime.ingestRecord(makeCanRxRecord(2, 0x120, 2000, 0));
        runtime.ingestRecord(makeCanRxRecord(3, 0x121, 3000, 1));
        const auto result = runtime.ingestRecord(makeCriticalRecord(TypedRecordType::BoardEvent, 5));
        const auto lastFrame = runtime.ingestRecord(makeCanRxRecord(4, 0x122, 4000, 1));

        QCOMPARE(lastFrame.projectedFrames.size(), 1);
        QCOMPARE(lastFrame.projectedFrames.at(0).canId, quint32(0x122));
        QCOMPARE(result.criticalRecords.size(), 1);
        QCOMPARE(result.criticalRecords.first().header.recordType, static_cast<quint8>(TypedRecordType::BoardEvent));
        QCOMPARE(lastFrame.status.observedCanRxFrames, quint64(4));
        QCOMPARE(lastFrame.status.projectedCanRxFrames, quint64(4));
        QCOMPARE(lastFrame.status.sampledCanRxFrames, quint64(0));
        QCOMPARE(lastFrame.status.workerDroppedCanRxFrames, quint64(0));
        QCOMPARE(lastFrame.status.observedBus0CanRxFrames, quint64(2));
        QCOMPARE(lastFrame.status.observedBus1CanRxFrames, quint64(2));
    }

    void liveProjectionRuntimeSamplesRoutineControlEvidence() {
        CanMonitorTransport::LiveProjectionRuntime runtime(8);

        QVector<TypedRecord> projected;
        auto ingest = [&](const TypedRecord& record) {
            const auto result = runtime.ingestRecord(record);
            for (const TypedRecord& critical : result.criticalRecords) {
                projected.push_back(critical);
            }
            return result;
        };

        ingest(makeControlAckRecord(1, 0x9001, 0x510, 1000, 0));
        ingest(makeControlAckRecord(2, 0x9002, 0x510, 1100, 0));
        ingest(makeCanTxRecord(3, 0x510, 1200, 0));
        ingest(makeCanTxRecord(4, 0x510, 1300, 0));
        ingest(makeCanRxRecord(5, 0x510, 1400, 0));
        QTest::qWait(260);
        const auto result = ingest(makeCanRxRecord(6, 0x510, 1500, 0));

        QCOMPARE(projected.size(), 4);
        QCOMPARE(projected.at(0).header.recordType, static_cast<quint8>(TypedRecordType::ControlAck));
        QCOMPARE(projected.at(0).header.seq, quint16(1));
        QCOMPARE(projected.at(1).header.recordType, static_cast<quint8>(TypedRecordType::ControlAck));
        QCOMPARE(projected.at(1).header.seq, quint16(2));
        QCOMPARE(projected.at(2).header.recordType, static_cast<quint8>(TypedRecordType::CanTxRaw));
        QCOMPARE(projected.at(2).header.seq, quint16(4));
        QCOMPARE(projected.at(3).header.recordType, static_cast<quint8>(TypedRecordType::CanRxRaw));
        QCOMPARE(projected.at(3).header.seq, quint16(6));
        QCOMPARE(result.status.observedControlEvidenceRecords, quint64(6));
        QCOMPARE(result.status.projectedControlEvidenceRecords, quint64(4));
        QCOMPARE(result.status.sampledControlEvidenceRecords, quint64(2));
    }

    void liveTruthRuntimeConsumesAllCanRxAndSnapshotsLatestPerBusKey() {
        CanMonitorTransport::LiveLatestRuntime runtime;

        runtime.ingestRecord(makeCanRxRecord(1, 0x120, 1000, 0));
        runtime.ingestRecord(makeCanRxRecord(2, 0x120, 1200, 0));
        runtime.ingestRecord(makeCanRxRecord(3, 0x120, 1100, 1));
        runtime.ingestRecord(makeCanRxRecord(4, 0x120, 2000, 0));

        CanMonitorTransport::LiveLatestRuntime::IngestResult result;
        result.frames = runtime.flush(true);
        result.status = runtime.status();

        QCOMPARE(result.frames.size(), 2);
        QCOMPARE(result.status.observedCanRxFrames, quint64(4));
        QCOMPARE(result.status.observedBus0CanRxFrames, quint64(3));
        QCOMPARE(result.status.observedBus1CanRxFrames, quint64(1));
        QCOMPARE(result.status.emittedLatestFrames, quint64(3));
        QCOMPARE(result.status.coalescedLatestUpdates, quint64(1));
        QCOMPARE(result.status.displayLoss, quint64(0));

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

    void captureCoreRuntimeOwnsMaterializedLiveLatestView() {
        CanMonitorTransport::CaptureCoreRuntime runtime;

        QByteArray bytes;
        bytes += makeTypedFrame(makeCanRxRecord(11, 0x121, 1100, 0));
        bytes += makeTypedFrame(makeCanRxRecord(12, 0x122, 1200, 1));

        QVector<CanMonitorTransport::DrainByteQueue::Block> blocks;
        blocks.push_back({bytes, 1});
        const auto result = runtime.ingestBlocks(blocks, 0, quint64(bytes.size()));

        QVERIFY(!result.viewChanges.isEmpty());
        const auto liveQuery = runtime.queryView({CanMonitorCore::CoreViewName::LiveLatest, 0, 0});
        QVERIFY(liveQuery.changed);
        QCOMPARE(liveQuery.change.viewName, CanMonitorCore::CoreViewName::LiveLatest);
        QCOMPARE(liveQuery.snapshot.severity, CanMonitorCore::CoreViewSeverity::Ok);

        const QJsonArray rows = liveQuery.snapshot.payload.value(QStringLiteral("frames")).toArray();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toObject().value(QStringLiteral("can_id")).toInt(), 0x121);
        QCOMPARE(rows.at(0).toObject().value(QStringLiteral("bus")).toInt(), 0);
        QCOMPARE(rows.at(1).toObject().value(QStringLiteral("can_id")).toInt(), 0x122);
        QCOMPARE(rows.at(1).toObject().value(QStringLiteral("bus")).toInt(), 1);

        const auto limited = runtime.queryView({CanMonitorCore::CoreViewName::LiveLatest, 0, 1});
        QCOMPARE(limited.snapshot.payload.value(QStringLiteral("frames")).toArray().size(), 1);

        const auto noChange = runtime.queryView({CanMonitorCore::CoreViewName::LiveLatest,
                                                 liveQuery.snapshot.viewSeq,
                                                 0});
        QVERIFY(!noChange.changed);
    }

    void typedEvidenceWorkerExposesCoreViewQueryPlane() {
        auto queue = QSharedPointer<CanMonitorTransport::DrainByteQueue>::create();
        QByteArray bytes;
        bytes += makeTypedFrame(makeCanRxRecord(31, 0x221, 3100, 0));
        bytes += makeTypedFrame(makeCanRxRecord(32, 0x222, 3200, 1));
        QVERIFY(queue->push(bytes));

        CanMonitorTransport::TypedEvidencePipelineWorkerRuntime worker(queue);
        QSignalSpy changedSpy(&worker, &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::coreViewChanged);
        QSignalSpy snapshotSpy(&worker, &CanMonitorTransport::TypedEvidencePipelineWorkerRuntime::coreViewSnapshotReady);

        worker.schedulePump(0);
        QTRY_VERIFY(changedSpy.size() >= 1);

        bool sawLiveLatest = false;
        for (const auto& signalArgs : changedSpy) {
            const QJsonObject change = signalArgs.at(0).toJsonObject();
            if (change.value(QStringLiteral("view_name")).toString() == QStringLiteral("live_latest")) {
                sawLiveLatest = true;
                QCOMPARE(change.value(QStringLiteral("cheap_counts")).toObject().value(QStringLiteral("key_count")).toInt(), 2);
                break;
            }
        }
        QVERIFY(sawLiveLatest);

        worker.queryCoreView(QStringLiteral("live_latest"), 0, 1, 1001);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        const auto snapshotArgs = snapshotSpy.takeFirst();
        QCOMPARE(snapshotArgs.at(0).toULongLong(), quint64(1001));
        QCOMPARE(snapshotArgs.at(1).toBool(), true);
        const QJsonObject snapshot = snapshotArgs.at(2).toJsonObject();
        const QJsonArray rows = snapshot.value(QStringLiteral("payload")).toObject().value(QStringLiteral("frames")).toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject().value(QStringLiteral("can_id")).toInt(), 0x222);

        const quint64 viewSeq = snapshot.value(QStringLiteral("view_seq")).toString().toULongLong();
        worker.queryCoreView(QStringLiteral("live_latest"), viewSeq, 0, 1002);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        const auto noChangeArgs = snapshotSpy.takeFirst();
        QCOMPARE(noChangeArgs.at(0).toULongLong(), quint64(1002));
        QCOMPARE(noChangeArgs.at(1).toBool(), false);

        worker.queryCoreView(QStringLiteral("missing_view"), 0, 0, 1003);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        const auto errorArgs = snapshotSpy.takeFirst();
        QCOMPARE(errorArgs.at(0).toULongLong(), quint64(1003));
        QCOMPARE(errorArgs.at(1).toBool(), false);
        QCOMPARE(errorArgs.at(2).toJsonObject().value(QStringLiteral("error")).toString(),
                 QStringLiteral("unknown_core_view"));
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
        session.updateLiveLatest(100, 80, 20, 50, 50, 3, 2, 4, 30, 2, 1, 0);
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
        liveTrace.insert(QStringLiteral("frames_received_emit_seq"), QStringLiteral("3"));
        liveTrace.insert(QStringLiteral("framesReceived_slot_seq"), QStringLiteral("3"));
        liveTrace.insert(QStringLiteral("framesReceived_slot_delay_ms"), 7);
        liveTrace.insert(QStringLiteral("framesReceived_slot_delay_max_ms"), 9);
        liveTrace.insert(QStringLiteral("framesReceived_calls"), QStringLiteral("3"));
        liveTrace.insert(QStringLiteral("pending_live_rows"), QStringLiteral("0"));
        liveTrace.insert(QStringLiteral("live_flush_timer_fire_count"), QStringLiteral("2"));
        liveTrace.insert(QStringLiteral("live_view_flush_timer_fire_count"), QStringLiteral("1"));
        liveTrace.insert(QStringLiteral("append_live_batch_frames"), QStringLiteral("4"));
        liveTrace.insert(QStringLiteral("live_model_rows"), QStringLiteral("4"));
        liveTrace.insert(QStringLiteral("app_snapshot_receive"), QStringLiteral("5"));
        liveTrace.insert(QStringLiteral("app_snapshot_ack"), QStringLiteral("5"));
        liveTrace.insert(QStringLiteral("app_snapshot_apply"), QStringLiteral("5"));
        liveTrace.insert(QStringLiteral("app_snapshot_apply_rows"), QStringLiteral("22"));
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
        drainTrace.insert(QStringLiteral("typedProjectionStatus_receive"), QStringLiteral("4"));
        drainTrace.insert(QStringLiteral("typedLiveLatestStatus_receive"), QStringLiteral("5"));
        drainTrace.insert(QStringLiteral("typedTransportStatus_receive"), QStringLiteral("6"));
        drainTrace.insert(QStringLiteral("app_typedProjectionStatus_receive"), QStringLiteral("4"));
        drainTrace.insert(QStringLiteral("app_typedLiveLatestStatus_receive"), QStringLiteral("5"));
        drainTrace.insert(QStringLiteral("app_typedTransportStatus_receive"), QStringLiteral("6"));
        drainTrace.insert(QStringLiteral("analysis_handoff_pending_frames"), QStringLiteral("7"));
        drainTrace.insert(QStringLiteral("analysis_handoff_complete_count"), QStringLiteral("8"));
        drainTrace.insert(QStringLiteral("analysis_handoff_complete_frames"), QStringLiteral("900"));
        drainTrace.insert(QStringLiteral("raw_ledger_handoff_pending_frames"), QStringLiteral("10"));
        drainTrace.insert(QStringLiteral("raw_ledger_handoff_pending_bytes"), QStringLiteral("2048"));
        drainTrace.insert(QStringLiteral("raw_ledger_handoff_complete_count"), QStringLiteral("11"));
        drainTrace.insert(QStringLiteral("raw_ledger_handoff_complete_frames"), QStringLiteral("1200"));
        drainTrace.insert(QStringLiteral("latest_handoff_emit_count"), QStringLiteral("12"));
        drainTrace.insert(QStringLiteral("latest_handoff_emit_frames"), QStringLiteral("1300"));
        drainTrace.insert(QStringLiteral("latest_handoff_pending_keys"), QStringLiteral("14"));
        session.updateDrainEventTrace(drainTrace);
        const QVariantList rows = session.rows();
        QCOMPARE(rows.size(), 16);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("passive_safety_profile"));
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("WARN"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("capture_storage"));
        QCOMPARE(rows.at(2).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("typed_parser"));
        QCOMPARE(rows.at(2).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("ERR"));
        QCOMPARE(rows.at(3).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("host_drain"));
        QVERIFY(rows.at(3).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("ready_max_us 100")));
        QCOMPARE(rows.at(4).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("capture_writer"));
        QCOMPARE(rows.at(5).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("analysis_queue"));
        QVERIFY(rows.at(5).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("processed 100")));
        QCOMPARE(rows.at(6).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("host_tx_queue"));
        QCOMPARE(rows.at(6).toMap().value(QStringLiteral("blocking")).toBool(), true);
        QCOMPARE(rows.at(7).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("board_health"));
        QCOMPARE(rows.at(8).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("csm_uplink"));
        QCOMPARE(rows.at(8).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("WARN"));
        QVERIFY(rows.at(8).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("large_pool 9/40")));
        QVERIFY(rows.at(8).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("can_q_high 11")));

        uplink.serialBackpressureTotal = 0;
        uplink.serialEnqueueFailTotal = 24;
        session.updateTypedStatus(10, 0, 0, 0, 0, 0);
        session.updateHostTxQueue(0, 0, 5, 2, 0);
        session.updateBoardHealth(0, 0, 100, uplink);
        QCOMPARE(session.level(), QStringLiteral("WARN"));
        QCOMPARE(session.rows().at(8).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("WARN"));
        QCOMPARE(session.rows().at(8).toMap().value(QStringLiteral("blocking")).toBool(), false);

        uplink.canTruthPoolAllocFailTotal = 1;
        session.updateBoardHealth(0, 0, 100, uplink);
        QCOMPARE(session.level(), QStringLiteral("ERR"));
        QCOMPARE(session.rows().at(8).toMap().value(QStringLiteral("blocking")).toBool(), true);
        uplink.canTruthPoolAllocFailTotal = 0;

        uplink.serialRingClearTotal = 1;
        session.updateBoardHealth(0, 0, 100, uplink);
        QCOMPARE(session.level(), QStringLiteral("ERR"));
        QCOMPARE(session.rows().at(8).toMap().value(QStringLiteral("blocking")).toBool(), true);

        QCOMPARE(rows.at(9).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("board_events"));
        QCOMPARE(rows.at(9).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("OK"));
        session.noteBoardEvent(9, 0x0618, 13, 123456);
        const QVariantList eventRows = session.rows();
        QCOMPARE(eventRows.at(9).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("WARN"));
        QVERIFY(eventRows.at(9).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("MCP2515 1")));
        QVERIFY(eventRows.at(9).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("0X0618:1")));
        QCOMPARE(rows.at(10).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_latest"));
        QVERIFY(rows.at(10).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("display_loss 0")));
        QCOMPARE(rows.at(11).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("decoded_can_tail"));
        QVERIFY(rows.at(11).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("segment_bytes 4096")));
        QCOMPARE(rows.at(12).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_projection"));
        QVERIFY(rows.at(12).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("budget_hits 3")));
        QCOMPARE(rows.at(13).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_delay"));
        QCOMPARE(rows.at(14).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("live_path_trace"));
        QVERIFY(rows.at(14).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("parsed 100")));
        QVERIFY(rows.at(14).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("seq 3/3")));
        QVERIFY(rows.at(14).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("snap 5/5/5")));
        QCOMPARE(rows.at(15).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("drain_event_trace"));
        QVERIFY(rows.at(15).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("readyRead/s 120")));
        QVERIFY(rows.at(15).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("statusRx proj/latest/typed 4/5/6")));
        QVERIFY(rows.at(15).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("analysis pend 7")));
        QVERIFY(rows.at(15).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("raw pend 10/2048B")));
        QVERIFY(rows.at(15).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("latest emit 12/1300")));
    }

    void transportSessionMapsCoreTransportSummary() {
        CanMonitorTransport::TransportSession session;
        session.setConnected(true);

        QJsonObject drainTrace;
        drainTrace.insert(QStringLiteral("drain_bytes_total"), QStringLiteral("12345"));
        drainTrace.insert(QStringLiteral("ready_read_count"), QStringLiteral("7"));
        drainTrace.insert(QStringLiteral("ready_read_max_us"), QStringLiteral("91"));
        drainTrace.insert(QStringLiteral("drain_burst_max_bytes"), QStringLiteral("4096"));
        drainTrace.insert(QStringLiteral("drain_queue_used_bytes"), QStringLiteral("64"));
        drainTrace.insert(QStringLiteral("drain_queue_max_used_bytes"), QStringLiteral("512"));
        drainTrace.insert(QStringLiteral("drain_queue_capacity_bytes"), QStringLiteral("16777216"));
        drainTrace.insert(QStringLiteral("drain_queue_overrun_bytes"), QStringLiteral("0"));
        drainTrace.insert(QStringLiteral("drain_queue_contention_count"), QStringLiteral("2"));

        QJsonObject payload;
        payload.insert(QStringLiteral("typed_frames"), QStringLiteral("42"));
        payload.insert(QStringLiteral("typed_bytes_dropped"), QStringLiteral("3"));
        payload.insert(QStringLiteral("typed_crc_failures"), QStringLiteral("1"));
        payload.insert(QStringLiteral("typed_length_failures"), QStringLiteral("0"));
        payload.insert(QStringLiteral("typed_version_warnings"), QStringLiteral("2"));
        payload.insert(QStringLiteral("typed_seq_gaps"), QStringLiteral("4"));
        payload.insert(QStringLiteral("projection_projected_can_rx"), QStringLiteral("11"));
        payload.insert(QStringLiteral("projection_sampled_can_rx"), QStringLiteral("5"));
        payload.insert(QStringLiteral("projection_dropped_can_rx"), QStringLiteral("1"));
        payload.insert(QStringLiteral("latest_observed_can_rx"), QStringLiteral("12"));
        payload.insert(QStringLiteral("latest_emitted_frames"), QStringLiteral("10"));
        payload.insert(QStringLiteral("latest_pending_keys"), 2);
        payload.insert(QStringLiteral("latest_display_loss"), QStringLiteral("0"));
        payload.insert(QStringLiteral("drain_event_trace"), drainTrace);

        session.updateCoreTransportSummary(payload);
        const QVariantList rows = session.rows();
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("passive_safety_profile"));
        QVERIFY(rows.at(2).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("frames 42")));
        QVERIFY(rows.at(2).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("drop 3")));
        QVERIFY(rows.at(3).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("bytes 12345 reads 7")));
        QVERIFY(rows.at(3).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("ready_max_us 91")));
        QVERIFY(rows.at(10).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("observed 12 emitted 10")));
        QVERIFY(rows.at(12).toMap().value(QStringLiteral("value")).toString().contains(QStringLiteral("projected 11")));
        QCOMPARE(session.parserFaultCount(), quint64(10));
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
