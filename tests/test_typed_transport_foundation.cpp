#include "StorageRuntime.h"
#include "TypedTransportParser.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

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

QByteArray makeTypedFrame(TypedRecordType type, quint16 seq, const QByteArray& payload) {
    QByteArray frame;
    frame.reserve(int(kTypedTransportFrameOverhead + payload.size()));
    frame.append(char(kTypedTransportSof0));
    frame.append(char(kTypedTransportSof1));
    frame.append(char(kTypedTransportVersion));
    frame.append(char(static_cast<quint8>(type)));
    frame.append(char(0));
    appendU16(frame, seq);
    appendU16(frame, quint16(payload.size()));
    frame.append(payload);
    const auto* crcStart = reinterpret_cast<const quint8*>(frame.constData() + 2);
    const quint16 crc = TypedTransportParser::crc16Ccitt(crcStart, frame.size() - 2);
    appendU16(frame, crc);
    return frame;
}

QByteArray makeCanPayload(quint64 monoUs = 2222, quint8 bus = 0, quint32 total = 1, quint32 dropped = 0) {
    QByteArray payload;
    payload.reserve(kTypedCanRawPayloadSize);
    appendU64(payload, monoUs);
    appendU32(payload, 0x530);
    payload.append(char(8));
    payload.append(char(bus));
    payload.append(QByteArray::fromHex("1122334455667788"));
    appendU32(payload, total);
    appendU32(payload, dropped);
    return payload;
}

QByteArray makeCanRxSegmentPayload() {
    QByteArray payload;
    payload.reserve(kTypedCanRxSegmentHeaderSize + 2 * kTypedCanRxSegmentEntrySize);
    appendU64(payload, 7);
    appendU64(payload, 100);
    appendU16(payload, 2);
    payload.append(char(kTypedCanRxSegmentEntrySize));
    payload.append(char(0));
    appendU32(payload, 0);
    appendU32(payload, 0);
    appendU32(payload, 0);

    auto appendEntry = [&payload](quint64 captureSeq, quint64 monoUs, quint8 bus, quint32 id, QByteArray data) {
        appendU64(payload, captureSeq);
        appendU64(payload, monoUs);
        appendU32(payload, id);
        payload.append(char(data.size()));
        payload.append(char(bus));
        payload.append(data.left(8));
        payload.append(QByteArray(8 - data.left(8).size(), char(0)));
    };
    appendEntry(100, 10'000, 0, 0x620, QByteArray::fromHex("5000000000000000"));
    appendEntry(101, 10'500, 1, 0x720, QByteArray::fromHex("4B01000000000000"));
    return payload;
}

QByteArray makeAdcPayload() {
    QByteArray payload;
    payload.reserve(kTypedAdcSamplePayloadSize);
    appendU64(payload, 3333);
    appendU32(payload, 9);
    appendU32(payload, 0);
    payload.append(char(0));
    payload.append(char(4));
    payload.append(char(12));
    payload.append(char(0x03));
    payload.append(QByteArray::fromHex("0001020300000000"));
    appendU16(payload, 1000);
    appendU16(payload, 2000);
    appendU16(payload, 3000);
    appendU16(payload, 4095);
    payload.append(QByteArray(8, char(0)));
    return payload;
}

QByteArray readFixtureStream() {
    QFile file(QStringLiteral(CAN_MONITOR_TEST_FIXTURES_DIR) + QStringLiteral("/typed_stream_v1.hex"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QByteArray hex = file.readAll();
    hex.replace(" ", "");
    hex.replace("\r", "");
    hex.replace("\n", "");
    hex.replace("\t", "");
    return QByteArray::fromHex(hex);
}

QByteArray readFileBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

quint64 readU64At(const QByteArray& bytes, qsizetype offset) {
    const auto* p = reinterpret_cast<const quint8*>(bytes.constData() + offset);
    return typedReadU64Le(p);
}

} // namespace

class TypedTransportFoundationTest : public QObject {
    Q_OBJECT

private slots:
    void parsesFixtureCanAndAdcRecords() {
        const QByteArray stream = readFixtureStream();
        QVERIFY(!stream.isEmpty());

        TypedTransportParser parser;
        parser.append(stream);

        const auto first = parser.takeOne();
        QVERIFY(first.has_value());
        QCOMPARE(first->header.version, quint8(1));
        QCOMPARE(first->header.recordType, quint8(1));
        QCOMPARE(first->header.seq, quint16(0));
        QCOMPARE(first->header.payloadLength, quint16(30));

        const auto can = decodeTypedCanRaw(*first);
        QVERIFY(can.has_value());
        QVERIFY(!can->txAudit);
        QCOMPARE(can->monoUs, quint64(123456));
        QCOMPARE(can->canId, quint32(0x530));
        QCOMPARE(can->dlc, quint8(8));
        QCOMPARE(can->bus, quint8(0));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(can->data), 8), QByteArray::fromHex("1122334455667788"));
        QCOMPARE(can->total, quint32(1));
        QCOMPARE(can->droppedOrFailed, quint32(0));

        const auto second = parser.takeOne();
        QVERIFY(second.has_value());
        QCOMPARE(second->header.recordType, quint8(5));
        QCOMPARE(second->header.seq, quint16(1));

        const auto adc = decodeTypedAdcSample(*second);
        QVERIFY(adc.has_value());
        QCOMPARE(adc->monoUs, quint64(123476));
        QCOMPARE(adc->sourceId, quint8(0));
        QCOMPARE(adc->channelCount, quint8(4));
        QCOMPARE(adc->resolutionBits, quint8(12));
        QCOMPARE(adc->flags, quint8(0x03));
        QCOMPARE(adc->channelId[0], quint8(0));
        QCOMPARE(adc->channelId[3], quint8(3));
        QCOMPARE(adc->raw[0], quint16(1000));
        QCOMPARE(adc->raw[3], quint16(4095));

        QVERIFY(!parser.takeOne().has_value());
        QCOMPARE(parser.counters().frames, quint64(2));
        QCOMPARE(parser.counters().crcFailures, quint64(0));
        QCOMPARE(parser.counters().lengthFailures, quint64(0));
        QCOMPARE(parser.counters().seqGaps, quint64(0));
    }

    void waitsForPartialFrame() {
        const QByteArray frame = makeTypedFrame(TypedRecordType::CanRxRaw, 9, makeCanPayload());
        TypedTransportParser parser;
        parser.append(frame.left(10));
        QVERIFY(!parser.takeOne().has_value());
        parser.append(frame.mid(10));
        const auto record = parser.takeOne();
        QVERIFY(record.has_value());
        QCOMPARE(record->header.seq, quint16(9));
        QCOMPARE(parser.counters().frames, quint64(1));
    }

    void includeFrameBytesDoesNotExposeDanglingPayload() {
        QByteArray frame = makeTypedFrame(TypedRecordType::CanRxRaw, 12, makeCanPayload(7777, 1, 3, 0));
        TypedTransportParser parser;
        parser.append(frame);

        auto record = parser.takeOne(true);
        QVERIFY(record.has_value());
        QVERIFY(!record->frameBytes.isEmpty());
        QVERIFY(record->payload.isEmpty());

        frame.fill(char(0));
        parser.reset();
        TypedRecord copied = *record;
        record.reset();

        const auto can = decodeTypedCanRaw(copied);
        QVERIFY(can.has_value());
        QCOMPARE(can->monoUs, quint64(7777));
        QCOMPARE(can->bus, quint8(1));
        QCOMPARE(can->total, quint32(3));
    }

    void drainsHighRateTypedCanStreamWithoutRetainingConsumedBytes() {
        QByteArray stream;
        constexpr int frameCount = 1800;
        stream.reserve(frameCount * int(kTypedTransportFrameOverhead + kTypedCanRawPayloadSize));
        for (int index = 0; index < frameCount; ++index) {
            stream += makeTypedFrame(TypedRecordType::CanRxRaw,
                                     quint16(index),
                                     makeCanPayload(1000 + quint64(index), quint8(index % 2), quint32(index + 1), 0));
        }

        TypedTransportParser parser;
        parser.append(stream);
        int parsed = 0;
        while (const auto record = parser.takeOne()) {
            const auto can = decodeTypedCanRaw(*record);
            QVERIFY(can.has_value());
            QCOMPARE(can->bus, quint8(parsed % 2));
            ++parsed;
        }

        QCOMPARE(parsed, frameCount);
        QCOMPARE(parser.counters().frames, quint64(frameCount));
        QCOMPARE(parser.counters().crcFailures, quint64(0));
        QCOMPARE(parser.counters().seqGaps, quint64(0));
        QCOMPARE(parser.bufferedBytes(), qsizetype(0));
    }

    void parsesCanRxSegmentRecords() {
        const QByteArray frame = makeTypedFrame(TypedRecordType::CanRxSegment, 55, makeCanRxSegmentPayload());
        TypedTransportParser parser;
        parser.append(frame);

        const auto record = parser.takeOne();
        QVERIFY(record.has_value());
        QCOMPARE(record->header.recordType, static_cast<quint8>(TypedRecordType::CanRxSegment));
        const auto header = decodeTypedCanRxSegmentHeader(*record);
        QVERIFY(header.has_value());
        QCOMPARE(header->segmentSeq, quint64(7));
        QCOMPARE(header->firstCaptureSeq, quint64(100));
        QCOMPARE(header->frameCount, quint16(2));
        QCOMPARE(typedCanRxFrameCount(*record), quint64(2));

        const auto first = decodeTypedCanRxSegmentEntry(*record, 0);
        const auto second = decodeTypedCanRxSegmentEntry(*record, 1);
        QVERIFY(first.has_value());
        QVERIFY(second.has_value());
        QCOMPARE(first->captureSeq, quint64(100));
        QCOMPARE(first->canId, quint32(0x620));
        QCOMPARE(first->bus, quint8(0));
        QCOMPARE(second->captureSeq, quint64(101));
        QCOMPARE(second->canId, quint32(0x720));
        QCOMPARE(second->bus, quint8(1));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(second->data), 8), QByteArray::fromHex("4B01000000000000"));
    }

    void resynchronizesAfterGarbageAndBadCrc() {
        QByteArray bad = makeTypedFrame(TypedRecordType::CanRxRaw, 2, makeCanPayload());
        bad[18] = char(quint8(bad[18]) ^ 0x55);
        const QByteArray good = makeTypedFrame(TypedRecordType::CanRxRaw, 3, makeCanPayload(4444, 1, 7, 0));

        TypedTransportParser parser;
        parser.append(QByteArray::fromHex("00FF12") + bad + QByteArray::fromHex("1337") + good);

        const auto record = parser.takeOne();
        QVERIFY(record.has_value());
        QCOMPARE(record->header.seq, quint16(3));
        const auto can = decodeTypedCanRaw(*record);
        QVERIFY(can.has_value());
        QCOMPARE(can->bus, quint8(1));
        QCOMPARE(can->total, quint32(7));
        QCOMPARE(parser.counters().crcFailures, quint64(1));
        QVERIFY(parser.counters().bytesDropped >= 4);
    }

    void rejectsOversizedLengthAndRecovers() {
        QByteArray invalid;
        invalid.append(char(kTypedTransportSof0));
        invalid.append(char(kTypedTransportSof1));
        invalid.append(char(kTypedTransportVersion));
        invalid.append(char(static_cast<quint8>(TypedRecordType::CanRxRaw)));
        invalid.append(char(0));
        appendU16(invalid, 20);
        appendU16(invalid, quint16(kTypedTransportMaxPayloadLength + 1));

        const QByteArray good = makeTypedFrame(TypedRecordType::AdcSample, 21, makeAdcPayload());
        TypedTransportParser parser;
        parser.append(invalid + good);

        const auto record = parser.takeOne();
        QVERIFY(record.has_value());
        QCOMPARE(record->header.recordType, quint8(5));
        QCOMPARE(record->header.seq, quint16(21));
        QCOMPARE(parser.counters().lengthFailures, quint64(1));
    }

    void storesExactTypedFramesAndSparseIndex() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray stream = readFixtureStream();
        QVERIFY(!stream.isEmpty());

        TypedTransportParser parser;
        parser.append(stream);

        StorageRuntime storage;
        QJsonObject meta;
        meta.insert(QStringLiteral("board_profile"), QStringLiteral("portenta-typed-v1"));
        QString error;
        QVERIFY2(storage.startTypedSession(dir.path(), meta, &error), qPrintable(error));

        while (const auto record = parser.takeOne()) {
            QVERIFY2(storage.appendTypedRecord(*record, &error), qPrintable(error));
        }
        QJsonObject event;
        event.insert(QStringLiteral("event"), QStringLiteral("test_note"));
        QVERIFY2(storage.appendEventJsonLine(event, &error), qPrintable(error));
        QVERIFY2(storage.finalizeTypedSession(&error), qPrintable(error));

        const auto paths = storage.paths();
        QVERIFY(QFileInfo::exists(paths.streamFinal));
        QVERIFY(QFileInfo::exists(paths.indexFinal));
        QVERIFY(QFileInfo::exists(paths.metaFinal));
        QVERIFY(QFileInfo::exists(paths.eventsFinal));
        QVERIFY(QFileInfo::exists(paths.diagnosticsFinal));
        QVERIFY(!QFileInfo::exists(paths.streamPart));
        QVERIFY(!QFileInfo::exists(paths.indexPart));

        QCOMPARE(readFileBytes(paths.streamFinal), stream);
        const QByteArray index = readFileBytes(paths.indexFinal);
        QCOMPARE(index.size(), 48);
        QCOMPARE(readU64At(index, 0), quint64(0));
        QCOMPARE(readU64At(index, 8), quint64(123456));
        QCOMPARE(quint8(index.at(16)), quint8(1));
        QCOMPARE(quint8(index.at(17)), quint8(0));
        QCOMPARE(typedReadU16Le(reinterpret_cast<const quint8*>(index.constData() + 18)), quint16(0));
        QCOMPARE(typedReadU16Le(reinterpret_cast<const quint8*>(index.constData() + 20)), quint16(30));

        const QJsonDocument metaDoc = QJsonDocument::fromJson(readFileBytes(paths.metaFinal));
        QVERIFY(metaDoc.isObject());
        QCOMPARE(metaDoc.object().value(QStringLiteral("format")).toString(), QStringLiteral("typed-evidence-stream-v1"));
        QCOMPARE(metaDoc.object().value(QStringLiteral("board_profile")).toString(), QStringLiteral("portenta-typed-v1"));
        QCOMPARE(metaDoc.object().value(QStringLiteral("diagnostics_file")).toString(), QStringLiteral("capture.diagnostics.json"));
        const QJsonDocument diagnosticsDoc = QJsonDocument::fromJson(readFileBytes(paths.diagnosticsFinal));
        QVERIFY(diagnosticsDoc.isObject());
        QCOMPARE(diagnosticsDoc.object().value(QStringLiteral("format")).toString(), QStringLiteral("typed-capture-diagnostics-v1"));
        QVERIFY(readFileBytes(paths.eventsFinal).contains("test_note"));
        QCOMPARE(storage.recordCount(), quint64(2));
        QCOMPARE(storage.bytesWritten(), quint64(stream.size()));
    }

    void decodesExtendedBoardHealthTransportCounters() {
        QByteArray payload(kTypedBoardHealthV5PayloadSize, char(0));
        auto putU32 = [&payload](qsizetype offset, quint32 value) {
            payload[offset + 0] = char(value & 0xFF);
            payload[offset + 1] = char((value >> 8) & 0xFF);
            payload[offset + 2] = char((value >> 16) & 0xFF);
            payload[offset + 3] = char((value >> 24) & 0xFF);
        };
        putU32(8, 1234);
        putU32(12, 2);
        putU32(16, 3);
        putU32(160, 4);
        putU32(164, 5);
        putU32(168, 36'772'683u);
        putU32(172, 6);
        putU32(176, 32768);
        putU32(180, 32);
        putU32(184, 7);
        putU32(188, 8);
        putU32(192, 9);
        putU32(196, 40);
        putU32(200, 10);
        putU32(204, 11);
        putU32(208, 12);
        putU32(212, 13);
        putU32(216, 14);
        putU32(220, 15);

        TypedRecord record;
        record.header.recordType = static_cast<quint8>(TypedRecordType::BoardHealth);
        record.header.payloadLength = quint16(payload.size());
        record.payload = payload;

        const auto health = decodeTypedBoardHealth(record);
        QVERIFY(health.has_value());
        QVERIFY(health->hasExtendedTransportCounters);
        QCOMPARE(health->canRxTotal, quint32(1234));
        QCOMPARE(health->serialEnqueueFailTotal, quint32(4));
        QCOMPARE(health->serialRingClearTotal, quint32(5));
        QCOMPARE(health->serialRingClearedBytesTotal, quint32(36'772'683u));
        QCOMPARE(health->serialBackpressureTotal, quint32(6));
        QCOMPARE(health->canSegmentEnqueueFailTotal, quint32(8));
        QVERIFY(health->hasUplinkPoolCounters);
        QCOMPARE(health->uplinkLargePoolUsedBlocks, quint32(9));
        QCOMPARE(health->uplinkLargePoolCapacityBlocks, quint32(40));
        QCOMPARE(health->uplinkLargePoolCanReserveUsedBlocks, quint32(10));
        QCOMPARE(health->canTruthDescriptorQueueHighWater, quint32(11));
        QCOMPARE(health->uplinkPoolAllocFailTotal, quint32(12));
        QCOMPARE(health->canTruthPoolAllocFailTotal, quint32(13));
        QCOMPARE(health->uplinkDescriptorHighWaterTotal, quint32(14));
        QCOMPARE(health->diagnosticSuppressedTotal, quint32(15));
    }
};

QTEST_APPLESS_MAIN(TypedTransportFoundationTest)

#include "test_typed_transport_foundation.moc"
