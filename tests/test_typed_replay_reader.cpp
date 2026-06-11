#include "StorageRuntime.h"
#include "TypedReplayReader.h"
#include "TypedTransportParser.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
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
    appendU16(frame, TypedTransportParser::crc16Ccitt(crcStart, frame.size() - 2));
    return frame;
}

QByteArray makeCanPayload(quint64 monoUs = 1000, quint8 bus = 0, quint32 total = 1, quint32 dropped = 0) {
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

} // namespace

class TypedReplayReaderTest : public QObject {
    Q_OBJECT

private slots:
    void loadsStoredStreamWithParitySummary() {
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
        QVERIFY2(storage.finalizeTypedSession(&error), qPrintable(error));

        TypedReplayReader reader;
        QVERIFY2(reader.loadFile(storage.paths().streamFinal, &error), qPrintable(error));
        QCOMPARE(reader.records().size(), 2);
        QCOMPARE(reader.summary().recordCount, quint64(2));
        QCOMPARE(reader.summary().bytesRead, quint64(stream.size()));
        QCOMPARE(reader.summary().typeCounts.value(static_cast<quint8>(TypedRecordType::CanRxRaw)), quint64(1));
        QCOMPARE(reader.summary().typeCounts.value(static_cast<quint8>(TypedRecordType::AdcSample)), quint64(1));
        QCOMPARE(reader.summary().firstSeq, quint16(0));
        QCOMPARE(reader.summary().lastSeq, quint16(1));
        QCOMPARE(reader.summary().firstMonoUs, quint64(123456));
        QCOMPARE(reader.summary().lastMonoUs, quint64(123476));
        QCOMPARE(reader.summary().crcFailures, quint64(0));
        QCOMPARE(reader.summary().lengthFailures, quint64(0));
        QCOMPARE(reader.summary().seqGaps, quint64(0));
        QCOMPARE(reader.faults().size(), 0);

        QCOMPARE(reader.records().at(0).offset, quint64(0));
        QCOMPARE(reader.records().at(1).offset, quint64(reader.records().at(0).record.frameBytes.size()));

        QByteArray reconstructed;
        for (const auto& entry : reader.records()) {
            reconstructed.append(entry.record.frameBytes);
        }
        QCOMPARE(reconstructed, readFileBytes(storage.paths().streamFinal));

        TypedReplayReader sessionReader;
        QVERIFY2(sessionReader.loadPath(storage.paths().sessionDir, &error), qPrintable(error));
        QCOMPARE(sessionReader.summary().metaPresent, true);
        QCOMPARE(sessionReader.summary().indexPresent, true);
        QCOMPARE(sessionReader.summary().eventsPresent, true);
        QCOMPARE(sessionReader.summary().indexEntryCount, quint64(2));
        QCOMPARE(sessionReader.summary().indexMismatchCount, quint64(0));
        QCOMPARE(sessionReader.summary().indexFirstOffset, quint64(0));
        QCOMPARE(sessionReader.summary().indexFirstMonoUs, quint64(123456));
        QCOMPARE(sessionReader.summary().indexLastMonoUs, quint64(123476));
        QCOMPARE(sessionReader.summary().durationUs, quint64(20));
        QCOMPARE(sessionReader.summary().captureState, QStringLiteral("FINALIZED"));
    }

    void reportsCrcFaultAndResynchronizesToNextValidFrame() {
        QByteArray bad = makeTypedFrame(TypedRecordType::CanRxRaw, 4, makeCanPayload(1000));
        bad[20] = char(quint8(bad[20]) ^ 0x7F);
        const QByteArray good = makeTypedFrame(TypedRecordType::CanRxRaw, 5, makeCanPayload(2000, 1, 2, 0));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("capture.stream"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bad);
        file.write(QByteArray::fromHex("0012"));
        file.write(good);
        file.close();

        QString error;
        TypedReplayReader reader;
        QVERIFY2(reader.loadFile(path, &error), qPrintable(error));
        QCOMPARE(reader.records().size(), 1);
        QCOMPARE(reader.records().first().record.header.seq, quint16(5));
        QCOMPARE(reader.records().first().monoUs, quint64(2000));
        QCOMPARE(reader.summary().crcFailures, quint64(1));
        QVERIFY(reader.summary().bytesDropped >= 1);
        QVERIFY(!reader.faults().isEmpty());
        QCOMPARE(reader.faults().first().code, QStringLiteral("crc"));
    }

    void reportsTrailingPartialFrameWithoutLosingValidRecords() {
        const QByteArray good = makeTypedFrame(TypedRecordType::CanRxRaw, 7, makeCanPayload(3000));
        const QByteArray partial = makeTypedFrame(TypedRecordType::CanRxRaw, 8, makeCanPayload(4000)).left(6);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("capture.stream"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(good);
        file.write(partial);
        file.close();

        QString error;
        TypedReplayReader reader;
        QVERIFY2(reader.loadFile(path, &error), qPrintable(error));
        QCOMPARE(reader.records().size(), 1);
        QCOMPARE(reader.records().first().record.header.seq, quint16(7));
        QCOMPARE(reader.summary().trailingBytes, quint64(partial.size()));
        QVERIFY(!reader.faults().isEmpty());
        QCOMPARE(reader.faults().last().code, QStringLiteral("incomplete_frame"));
    }

    void loadsPartialSessionDirectoryAndReportsSidecars() {
        const QByteArray good = makeTypedFrame(TypedRecordType::CanRxRaw, 9, makeCanPayload(5000));
        const QByteArray partial = makeTypedFrame(TypedRecordType::BoardHealth, 10, QByteArray(52, char(0x11))).left(5);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString sessionDir = dir.filePath(QStringLiteral("field_session.typed"));
        QVERIFY(QDir().mkpath(sessionDir));

        QFile meta(sessionDir + QStringLiteral("/session.meta.json.part"));
        QVERIFY(meta.open(QIODevice::WriteOnly | QIODevice::Truncate));
        meta.write("{\"format\":\"typed-evidence-stream-v1\",\"stream_file\":\"capture.stream\"}");
        meta.close();

        QFile index(sessionDir + QStringLiteral("/capture.index.part"));
        QVERIFY(index.open(QIODevice::WriteOnly | QIODevice::Truncate));
        index.write(QByteArray(24, char(0)));
        index.close();

        QFile events(sessionDir + QStringLiteral("/events.jsonl.part"));
        QVERIFY(events.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        events.write("{\"event\":\"power_loss\"}\n");
        events.close();

        QFile stream(sessionDir + QStringLiteral("/capture.stream.part"));
        QVERIFY(stream.open(QIODevice::WriteOnly | QIODevice::Truncate));
        stream.write(good);
        stream.write(partial);
        stream.close();

        QString error;
        TypedReplayReader reader;
        QVERIFY2(reader.loadPath(sessionDir, &error), qPrintable(error));
        QCOMPARE(reader.records().size(), 1);
        QCOMPARE(reader.summary().streamPart, true);
        QCOMPARE(reader.summary().partialCapture, true);
        QCOMPARE(reader.summary().captureState, QStringLiteral("PARTIAL"));
        QCOMPARE(reader.summary().metaPresent, true);
        QCOMPARE(reader.summary().indexPresent, true);
        QCOMPARE(reader.summary().eventsPresent, true);
        QCOMPARE(reader.summary().indexEntryCount, quint64(1));
        QCOMPARE(reader.summary().indexPart, true);
        QVERIFY(reader.summary().indexMismatchCount > 0);
        QCOMPARE(reader.summary().eventLineCount, quint64(1));
        QCOMPARE(reader.summary().eventsPart, true);
        QVERIFY(reader.summary().lastEventText.contains(QStringLiteral("power_loss")));
        QCOMPARE(reader.summary().trailingBytes, quint64(partial.size()));
        QVERIFY(reader.summary().streamPath.endsWith(QStringLiteral("capture.stream.part")));
        QVERIFY(reader.summary().diagnosticSummary.contains(QStringLiteral("PARTIAL")));
        QVERIFY(reader.summary().diagnosticSummary.contains(QStringLiteral("index 1")));
    }
};

QTEST_APPLESS_MAIN(TypedReplayReaderTest)

#include "test_typed_replay_reader.moc"
