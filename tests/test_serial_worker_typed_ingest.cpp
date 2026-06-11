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
        qRegisterMetaType<TypedRecordList>("TypedRecordList");
        qRegisterMetaType<FrameRecordList>("FrameRecordList");
    }

    void typedModeParsesTypedFramesWithoutLegacyFrames() {
        SerialWorker worker;
        worker.setTransportMode(SerialWorker::TransportMode::TypedEvidence);

        QSignalSpy typedSpy(&worker, &SerialWorker::typedRecordsReceived);
        QSignalSpy statusSpy(&worker, &SerialWorker::typedTransportStatusChanged);
        QSignalSpy projectionSpy(&worker, &SerialWorker::framesReceived);
        QSignalSpy projectionStatusSpy(&worker, &SerialWorker::typedProjectionStatusChanged);

        const QByteArray frame = makeTypedFrame(TypedRecordType::CanRxRaw, 10, makeCanPayload(5000, 2));
        worker.ingestBytesForTest(frame.left(12));
        QCOMPARE(typedSpy.size(), 0);
        QCOMPARE(projectionSpy.size(), 0);

        worker.ingestBytesForTest(frame.mid(12));
        QCOMPARE(typedSpy.size(), 0);
        QTRY_COMPARE(projectionSpy.size(), 1);
        QVERIFY(projectionStatusSpy.size() >= 1);
        QVERIFY(statusSpy.size() >= 1);

        const auto frames = qvariant_cast<FrameRecordList>(projectionSpy.takeFirst().at(0));
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().tExtUs, quint64(5000));
        QCOMPARE(frames.first().bus, quint8(2));
        QCOMPARE(frames.first().canId, quint32(0x530));

        const auto status = statusSpy.takeLast();
        QCOMPARE(status.at(0).toULongLong(), quint64(1));
        QCOMPARE(status.at(2).toULongLong(), quint64(0));
        QCOMPARE(status.at(3).toULongLong(), quint64(0));

        const auto projectionStatus = projectionStatusSpy.takeLast();
        QCOMPARE(projectionStatus.at(0).toULongLong(), quint64(1));
        QCOMPARE(projectionStatus.at(1).toULongLong(), quint64(1));
        QCOMPARE(projectionStatus.at(2).toULongLong(), quint64(0));
    }

    void typedModeReportsCrcFailureAndRecovers() {
        SerialWorker worker;
        worker.setTransportMode(SerialWorker::TransportMode::TypedEvidence);

        QSignalSpy typedSpy(&worker, &SerialWorker::typedRecordsReceived);
        QSignalSpy statusSpy(&worker, &SerialWorker::typedTransportStatusChanged);
        QSignalSpy projectionSpy(&worker, &SerialWorker::framesReceived);

        QByteArray bad = makeTypedFrame(TypedRecordType::CanRxRaw, 20, makeCanPayload(6000, 0));
        bad[18] = char(quint8(bad[18]) ^ 0x55);
        const QByteArray good = makeTypedFrame(TypedRecordType::CanRxRaw, 21, makeCanPayload(7000, 1));

        worker.ingestBytesForTest(bad + good);

        QCOMPARE(typedSpy.size(), 0);
        QTRY_COMPARE(projectionSpy.size(), 1);
        const auto frames = qvariant_cast<FrameRecordList>(projectionSpy.takeFirst().at(0));
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().seq, quint8(21));

        QVERIFY(statusSpy.size() >= 1);
        const auto status = statusSpy.takeLast();
        QCOMPARE(status.at(0).toULongLong(), quint64(1));
        QVERIFY(status.at(1).toULongLong() >= 1);
        QCOMPARE(status.at(2).toULongLong(), quint64(1));
    }

    void liveProjectionQueuesLatestFramePerKeyBetweenFlushes() {
        SerialWorker worker;
        worker.setTransportMode(SerialWorker::TransportMode::TypedEvidence);

        QSignalSpy projectionSpy(&worker, &SerialWorker::framesReceived);
        QSignalSpy projectionStatusSpy(&worker, &SerialWorker::typedProjectionStatusChanged);

        worker.ingestBytesForTest(makeTypedFrame(TypedRecordType::CanRxRaw, 40, makeCanPayload(1000, 1)));
        QTRY_COMPARE(projectionSpy.size(), 1);
        projectionSpy.clear();
        projectionStatusSpy.clear();

        worker.ingestBytesForTest(makeTypedFrame(TypedRecordType::CanRxRaw, 41, makeCanPayload(2000, 1)));
        worker.ingestBytesForTest(makeTypedFrame(TypedRecordType::CanRxRaw, 42, makeCanPayload(3000, 1)));

        QTRY_COMPARE(projectionSpy.size(), 1);
        const auto frames = qvariant_cast<FrameRecordList>(projectionSpy.takeFirst().at(0));
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().tExtUs, quint64(3000));
        QCOMPARE(frames.first().seq, quint8(42));

        QVERIFY(projectionStatusSpy.size() >= 1);
        const auto projectionStatus = projectionStatusSpy.takeLast();
        QVERIFY(projectionStatus.at(2).toULongLong() >= quint64(1));
    }

    void liveTruthFramesPreserveObservedRawGapAcrossProjectionSampling() {
        SerialWorker worker;
        worker.setTransportMode(SerialWorker::TransportMode::TypedEvidence);

        QSignalSpy truthSpy(&worker, &SerialWorker::truthFramesReceived);

        worker.ingestBytesForTest(makeTypedFrame(TypedRecordType::CanRxRaw, 50, makeCanPayload(1000, 1)));
        QTRY_COMPARE(truthSpy.size(), 1);
        truthSpy.clear();

        worker.ingestBytesForTest(makeTypedFrame(TypedRecordType::CanRxRaw, 51, makeCanPayload(1100, 1)));
        worker.ingestBytesForTest(makeTypedFrame(TypedRecordType::CanRxRaw, 52, makeCanPayload(1200, 1)));

        QTRY_COMPARE(truthSpy.size(), 1);
        const auto frames = qvariant_cast<FrameRecordList>(truthSpy.takeFirst().at(0));
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().tExtUs, quint64(1200));
        QCOMPARE(frames.first().seq, quint8(52));
        QCOMPARE(frames.first().hasObservedGap, true);
        QCOMPARE(frames.first().observedGapUs, quint64(100));
    }

    void typedStorageWritesFinalizedEvidenceSession() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString sessionDir = tempDir.path() + QStringLiteral("/capture.typed");

        SerialWorker worker;
        worker.setTransportMode(SerialWorker::TransportMode::TypedEvidence);

        QSignalSpy storageStateSpy(&worker, &SerialWorker::typedStorageStateChanged);
        QSignalSpy storageProgressSpy(&worker, &SerialWorker::typedStorageProgress);

        QJsonObject meta;
        meta.insert(QStringLiteral("test"), true);
        QVERIFY(worker.setTypedStorage(true, sessionDir, meta));

        const QByteArray frame1 = makeTypedFrame(TypedRecordType::CanRxRaw, 1, makeCanPayload(1000, 0));
        const QByteArray frame2 = makeTypedFrame(TypedRecordType::CanTxRaw, 2, makeCanPayload(2000, 1));
        worker.ingestBytesForTest(frame1 + frame2);

        QVERIFY(worker.setTypedStorage(false, sessionDir, QJsonObject{}));
        QVERIFY(storageStateSpy.size() >= 2);
        QVERIFY(storageProgressSpy.size() >= 2);

        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.stream")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/capture.index")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/session.meta.json")));
        QVERIFY(QFileInfo::exists(sessionDir + QStringLiteral("/events.jsonl")));
        QVERIFY(!QFileInfo::exists(sessionDir + QStringLiteral("/capture.stream.part")));

        TypedReplayReader reader;
        QString error;
        QVERIFY2(reader.loadFile(sessionDir + QStringLiteral("/capture.stream"), &error), qPrintable(error));
        QCOMPARE(reader.records().size(), 2);
        QCOMPARE(reader.records().at(0).record.header.seq, quint16(1));
        QCOMPARE(reader.records().at(1).record.header.seq, quint16(2));
        QCOMPARE(reader.summary().typeCounts.value(quint8(TypedRecordType::CanRxRaw)), quint64(1));
        QCOMPARE(reader.summary().typeCounts.value(quint8(TypedRecordType::CanTxRaw)), quint64(1));
        QCOMPARE(reader.summary().firstMonoUs, quint64(1000));
        QCOMPARE(reader.summary().lastMonoUs, quint64(2000));
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
        QVERIFY(!QFileInfo::exists(sessionDir + QStringLiteral("/capture.stream.part")));
    }
};

QTEST_MAIN(SerialWorkerTypedIngestTest)

#include "test_serial_worker_typed_ingest.moc"
