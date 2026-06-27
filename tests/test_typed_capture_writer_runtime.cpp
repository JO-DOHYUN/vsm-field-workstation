#include "TypedReplayReader.h"
#include "TypedTransportParser.h"
#include "transport/TypedCaptureWriterRuntime.h"

#include <QFileInfo>
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

QByteArray makeCanPayload(quint64 monoUs) {
    QByteArray payload;
    appendU64(payload, monoUs);
    appendU32(payload, 0x530);
    payload.append(char(8));
    payload.append(char(0));
    payload.append(QByteArray::fromHex("1122334455667788"));
    appendU32(payload, 1);
    appendU32(payload, 0);
    return payload;
}
}

class TypedCaptureWriterRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void writesQueuedTypedRecordsAndDiagnostics() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        CanMonitorTransport::TypedCaptureWriterRuntime writer;
        QJsonObject meta;
        meta.insert(QStringLiteral("writer_test"), true);
        const QString sessionDir = tempDir.path() + QStringLiteral("/capture.typed");
        auto start = writer.startStorage(sessionDir, meta);
        QVERIFY2(start.ok, qPrintable(start.error));

        TypedTransportParser parser;
        parser.append(makeTypedFrame(TypedRecordType::CanRxRaw, 1, makeCanPayload(1000)) +
                      makeTypedFrame(TypedRecordType::CanTxRaw, 2, makeCanPayload(2000)));
        TypedRecordList records;
        while (auto record = parser.takeOne()) records.push_back(*record);
        QCOMPARE(records.size(), 2);

        TypedCaptureFrameList frames;
        frames.reserve(records.size());
        for (const TypedRecord& record : records) {
            frames.push_back(TypedCaptureFrame{record.header, record.frameBytes, typedRecordMonoUs(record)});
        }

        auto append = writer.enqueueFrames(std::move(frames));
        QVERIFY2(append.ok, qPrintable(append.error));

        QJsonObject diagnostics;
        diagnostics.insert(QStringLiteral("format"), QStringLiteral("typed-capture-diagnostics-v1"));
        QJsonObject parserJson;
        parserJson.insert(QStringLiteral("frames"), QStringLiteral("2"));
        parserJson.insert(QStringLiteral("seq_gaps"), QStringLiteral("0"));
        diagnostics.insert(QStringLiteral("parser"), parserJson);
        auto stop = writer.finalizeStorageIfActive(diagnostics);
        QVERIFY2(stop.ok, qPrintable(stop.error));

        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.stream")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.index")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.diagnostics.json")));

        TypedReplayReader reader;
        QString error;
        QVERIFY2(reader.loadPath(sessionDir, &error), qPrintable(error));
        QCOMPARE(reader.records().size(), 2);
        QCOMPARE(reader.summary().diagnosticsPresent, true);
        QCOMPARE(reader.summary().liveParserFrames, quint64(2));
    }
};

QTEST_APPLESS_MAIN(TypedCaptureWriterRuntimeTest)

#include "test_typed_capture_writer_runtime.moc"
