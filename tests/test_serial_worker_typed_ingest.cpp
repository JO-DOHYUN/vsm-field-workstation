#include "SerialWorker.h"
#include "TypedReplayReader.h"
#include "TypedTransportParser.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QSignalSpy>
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

QByteArray makeCanPayload(quint64 monoUs, quint8 bus) {
    QByteArray payload;
    payload.reserve(kTypedCanRawPayloadSize);
    appendU64(payload, monoUs);
    appendU32(payload, 0x530);
    payload.append(char(8));
    payload.append(char(bus));
    payload.append(QByteArray::fromHex("1122334455667788"));
    appendU32(payload, 1);
    appendU32(payload, 0);
    return payload;
}

} // namespace

class SerialWorkerTypedIngestTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<TypedRecord>("TypedRecord");
        qRegisterMetaType<FrameRecordList>("FrameRecordList");
    }

    void typedModeParsesTypedFramesWithoutLegacyFrames() {
        SerialWorker worker;
        worker.setTransportMode(SerialWorker::TransportMode::TypedEvidence);

        QSignalSpy statusSpy(&worker, &SerialWorker::typedTransportStatusChanged);

        const QByteArray frame = makeTypedFrame(TypedRecordType::CanRxRaw, 10, makeCanPayload(5000, 2));
        worker.ingestBytesForTest(frame.left(12));
        QCOMPARE(statusSpy.size(), 0);

        worker.ingestBytesForTest(frame.mid(12));
        QVERIFY(statusSpy.size() >= 1);

        const auto status = statusSpy.takeLast();
        QCOMPARE(status.at(0).toULongLong(), quint64(1));
        QCOMPARE(status.at(2).toULongLong(), quint64(0));
        QCOMPARE(status.at(3).toULongLong(), quint64(0));
    }

    void typedModeReportsCrcFailureAndRecovers() {
        SerialWorker worker;
        worker.setTransportMode(SerialWorker::TransportMode::TypedEvidence);

        QSignalSpy statusSpy(&worker, &SerialWorker::typedTransportStatusChanged);

        QByteArray bad = makeTypedFrame(TypedRecordType::CanRxRaw, 20, makeCanPayload(6000, 0));
        bad[18] = char(quint8(bad[18]) ^ 0x55);
        const QByteArray good = makeTypedFrame(TypedRecordType::CanRxRaw, 21, makeCanPayload(7000, 1));

        worker.ingestBytesForTest(bad + good);

        QVERIFY(statusSpy.size() >= 1);
        const auto status = statusSpy.takeLast();
        QCOMPARE(status.at(0).toULongLong(), quint64(1));
        QVERIFY(status.at(1).toULongLong() >= 1);
        QCOMPARE(status.at(2).toULongLong(), quint64(1));
    }

    void serialWorkerDirectIngestDoesNotOwnTypedCaptureTruth() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString sessionDir = tempDir.path() + QStringLiteral("/capture.typed");

        SerialWorker worker;
        worker.setTransportMode(SerialWorker::TransportMode::TypedEvidence);

        QSignalSpy storageStateSpy(&worker, &SerialWorker::typedStorageStateChanged);
        QSignalSpy storageProgressSpy(&worker, &SerialWorker::typedStorageProgress);
        QSignalSpy errorSpy(&worker, &SerialWorker::errorOccurred);

        QJsonObject meta;
        meta.insert(QStringLiteral("test"), true);
        QVERIFY(worker.setTypedStorage(true, sessionDir, meta));

        const QByteArray frame1 = makeTypedFrame(TypedRecordType::CanRxRaw, 1, makeCanPayload(1000, 0));
        const QByteArray frame2 = makeTypedFrame(TypedRecordType::CanTxRaw, 2, makeCanPayload(2000, 1));
        worker.ingestBytesForTest(frame1 + frame2);

        const bool stopped = worker.setTypedStorage(false, sessionDir, QJsonObject{});
        const QString stopError = errorSpy.isEmpty() ? QString() : errorSpy.takeLast().at(0).toString();
        QVERIFY2(stopped, qPrintable(stopError));
        QVERIFY(storageStateSpy.size() >= 2);
        QVERIFY(storageProgressSpy.size() >= 2);

        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.stream")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.index")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/session.meta.json")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/events.jsonl")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.diagnostics.json")));
        QVERIFY(!QFileInfo::exists(sessionDir + QStringLiteral("/capture.stream.part")));

        TypedReplayReader reader;
        QString error;
        QVERIFY(!reader.loadFile(sessionDir + QStringLiteral("/capture.stream"), &error));
        QVERIFY(error.contains(QStringLiteral("No valid typed replay records")));
    }

    void stopFinalizesActiveTypedStorageSession() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString sessionDir = tempDir.path() + QStringLiteral("/stop_capture.typed");

        SerialWorker worker;
        worker.setTransportMode(SerialWorker::TransportMode::TypedEvidence);

        QSignalSpy storageStateSpy(&worker, &SerialWorker::typedStorageStateChanged);
        QSignalSpy storageProgressSpy(&worker, &SerialWorker::typedStorageProgress);

        QJsonObject meta;
        meta.insert(QStringLiteral("stop_finalize"), true);
        QVERIFY(worker.setTypedStorage(true, sessionDir, meta));
        worker.ingestBytesForTest(makeTypedFrame(TypedRecordType::CanRxRaw, 31, makeCanPayload(3100, 1)));

        worker.stop();

        QVERIFY(storageStateSpy.size() >= 2);
        const auto lastState = storageStateSpy.takeLast();
        QCOMPARE(lastState.at(0).toBool(), false);
        QCOMPARE(lastState.at(1).toString(), sessionDir);
        QVERIFY(storageProgressSpy.size() >= 2);
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.stream")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.index")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/session.meta.json")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/events.jsonl")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.diagnostics.json")));
        QVERIFY(!QFileInfo::exists(sessionDir + QStringLiteral("/capture.stream.part")));
    }
};

QTEST_MAIN(SerialWorkerTypedIngestTest)

#include "test_serial_worker_typed_ingest.moc"
