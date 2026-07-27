#include "core/CaptureCoreProcessRuntime.h"
#include "core/CoreIpcClientRuntime.h"
#include "transport/CaptureCoreRuntime.h"
#include "TypedTransportParser.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

#include <limits>

namespace {

void appendU16(QByteArray& out, quint16 value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendU32(QByteArray& out, quint32 value) {
    for (int byte = 0; byte < 4; ++byte) {
        out.append(char((value >> (byte * 8)) & 0xFF));
    }
}

void appendU64(QByteArray& out, quint64 value) {
    for (int byte = 0; byte < 8; ++byte) {
        out.append(char((value >> (byte * 8)) & 0xFF));
    }
}

QByteArray makeUnknownSegmentSchemaFrame() {
    QByteArray payload;
    appendU64(payload, 1);
    appendU64(payload, 10);
    appendU16(payload, 0);
    payload.append(char(kTypedCanRxSegmentCompactEntrySize));
    payload.append(char(0x01));
    appendU32(payload, 0);
    appendU32(payload, 0);
    payload.append(char(9));
    payload.append(char(kTypedCanRxSegmentCompactHeaderSize));
    appendU16(payload, 0);
    appendU64(payload, 1000);

    QByteArray frame;
    frame.append(char(kTypedTransportSof0));
    frame.append(char(kTypedTransportSof1));
    frame.append(char(kTypedTransportVersion));
    frame.append(char(static_cast<quint8>(TypedRecordType::CanRxSegment)));
    frame.append(char(0));
    appendU16(frame, 1);
    appendU16(frame, quint16(payload.size()));
    frame.append(payload);
    const auto* crcStart = reinterpret_cast<const quint8*>(frame.constData() + 2);
    appendU16(frame, TypedTransportParser::crc16Ccitt(crcStart, frame.size() - 2));
    return frame;
}

QByteArray makeSchema2SegmentFrame(quint8 flags,
                                   quint8 dlc,
                                   quint64 baseMonoUs,
                                   quint32 monoDeltaUs,
                                   bool appendTrailingByte = false) {
    QByteArray payload;
    appendU64(payload, 2);
    appendU64(payload, 20);
    appendU16(payload, 1);
    payload.append(char(kTypedCanRxSegmentCompactEntrySize));
    payload.append(char(flags));
    appendU32(payload, 0);
    appendU32(payload, 0);
    payload.append(char(kTypedCanRxSegmentCompactSchema));
    payload.append(char(kTypedCanRxSegmentCompactHeaderSize));
    appendU16(payload, 0);
    appendU64(payload, baseMonoUs);
    appendU16(payload, 0);
    appendU32(payload, monoDeltaUs);
    appendU32(payload, 0x123);
    payload.append(char(dlc));
    payload.append(char(0));
    payload.append(QByteArray::fromHex("1122334455667788"));
    if (appendTrailingByte) payload.append(char(0));

    QByteArray frame;
    frame.append(char(kTypedTransportSof0));
    frame.append(char(kTypedTransportSof1));
    frame.append(char(kTypedTransportVersion));
    frame.append(char(static_cast<quint8>(TypedRecordType::CanRxSegment)));
    frame.append(char(0));
    appendU16(frame, 2);
    appendU16(frame, quint16(payload.size()));
    frame.append(payload);
    const auto* crcStart = reinterpret_cast<const quint8*>(frame.constData() + 2);
    appendU16(frame, TypedTransportParser::crc16Ccitt(crcStart, frame.size() - 2));
    return frame;
}

} // namespace

class CaptureCoreProcessRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void servesInitialViewsOverIpc() {
        const QString serverName = QStringLiteral("vsm-core-process-test-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(reinterpret_cast<quintptr>(this));

        CanMonitorCore::CaptureCoreProcessRuntime runtime;
        QString error;
        QVERIFY2(runtime.startIpc(serverName, &error), qPrintable(error));
        QVERIFY(runtime.isIpcListening());

        CanMonitorCore::CoreIpcClientRuntime client;
        QSignalSpy connectedSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::connectedChanged);
        QSignalSpy snapshotSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::viewSnapshotReceived);

        client.connectToServer(serverName);
        QTRY_VERIFY(client.isConnected());
        QVERIFY(!connectedSpy.isEmpty());

        const quint64 healthRequest = client.requestView(QStringLiteral("core_health"), 0, 1);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        auto args = snapshotSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), healthRequest);
        QCOMPARE(args.at(1).toBool(), true);
        QJsonObject snapshot = args.at(2).toJsonObject();
        QCOMPARE(snapshot.value(QStringLiteral("payload")).toObject().value(QStringLiteral("process")).toString(),
                 QStringLiteral("vsm-capture-core"));

        const quint64 transportRequest = client.requestView(QStringLiteral("transport_summary"), 0, 1);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        args = snapshotSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), transportRequest);
        QCOMPARE(args.at(1).toBool(), true);
        snapshot = args.at(2).toJsonObject();
        QCOMPARE(snapshot.value(QStringLiteral("payload")).toObject().value(QStringLiteral("serial_owner")).toString(),
                 QStringLiteral("core"));

        const quint64 rawLedgerRequest = client.requestView(QStringLiteral("raw_ledger_tail"), 0, 16);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        args = snapshotSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), rawLedgerRequest);
        QCOMPARE(args.at(1).toBool(), true);
        snapshot = args.at(2).toJsonObject();
        const QJsonObject rawPayload = snapshot.value(QStringLiteral("payload")).toObject();
        QCOMPARE(rawPayload.value(QStringLiteral("source")).toString(),
                 QStringLiteral("decoded_can_tail_view"));
        QCOMPARE(rawPayload.value(QStringLiteral("frames")).toArray().size(), 0);

        client.disconnectFromServer();
        runtime.stopIpc();
    }

    void unknownCanRxSegmentSchemaIsOperatorVisible() {
        CanMonitorTransport::CaptureCoreRuntime runtime;
        QVector<CanMonitorTransport::DrainByteQueue::Block> blocks;
        const QByteArray frame = makeUnknownSegmentSchemaFrame();
        blocks.push_back({frame, 1});

        const auto result = runtime.ingestBlocks(blocks, 0, quint64(frame.size()));
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(result.errors.first().contains(
            QStringLiteral("CAN_RX_SEGMENT rejected: CAN_RX_SEGMENT unsupported schema=9")));
    }

    void malformedCompactSegmentIsOperatorVisible() {
        const quint8 validFlags = kTypedCanRxSegmentFlagCaptureSequenceValid |
            kTypedCanRxSegmentFlagCompactEntries;
        const auto ingest = [](const QByteArray& frame) {
            CanMonitorTransport::CaptureCoreRuntime runtime;
            QVector<CanMonitorTransport::DrainByteQueue::Block> blocks;
            blocks.push_back({frame, 1});
            return runtime.ingestBlocks(blocks, 0, quint64(frame.size()));
        };

        auto result = ingest(makeSchema2SegmentFrame(
            kTypedCanRxSegmentFlagCaptureSequenceValid, 8, 1000, 1));
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(result.errors.first().contains(QStringLiteral("flags=0x01 missing 0x03")));

        result = ingest(makeSchema2SegmentFrame(validFlags, 9, 1000, 1));
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(result.errors.first().contains(QStringLiteral("rejected at entry 0")));
        QVERIFY(result.errors.first().contains(QStringLiteral("dlc=9 exceeds 8")));

        result = ingest(makeSchema2SegmentFrame(
            validFlags,
            8,
            std::numeric_limits<quint64>::max() - 1,
            2));
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(result.errors.first().contains(QStringLiteral("rejected at entry 0")));
        QVERIFY(result.errors.first().contains(QStringLiteral("delta overflow")));

        result = ingest(makeSchema2SegmentFrame(validFlags, 8, 1000, 1, true));
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(result.errors.first().contains(QStringLiteral("payload_size=61, expected 60")));
    }
};

QTEST_MAIN(CaptureCoreProcessRuntimeTest)
#include "test_capture_core_process_runtime.moc"
