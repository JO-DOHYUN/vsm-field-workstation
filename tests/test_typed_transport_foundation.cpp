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
        QVERIFY(readFileBytes(paths.eventsFinal).contains("test_note"));
        QCOMPARE(storage.recordCount(), quint64(2));
        QCOMPARE(storage.bytesWritten(), quint64(stream.size()));
    }
};

QTEST_APPLESS_MAIN(TypedTransportFoundationTest)

#include "test_typed_transport_foundation.moc"
