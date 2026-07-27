#include "TypedRecords.h"
#include "analysis/AnalysisRuntime.h"

#include <QtTest/QtTest>

#include <cstring>

using CanMonitorAnalysis::AnalysisRuntime;

namespace {

FrameRecord makeFrame(quint8 bus, quint32 id, quint64 us, quint8 dlc, std::initializer_list<quint8> bytes) {
    FrameRecord frame;
    frame.bus = bus;
    frame.canId = id;
    frame.tExtUs = us;
    frame.dlc = dlc;
    int index = 0;
    for (quint8 value : bytes) {
        if (index >= 8) break;
        frame.data[index++] = value;
    }
    return frame;
}

CanModel::RuleSpec makeRule(quint32 id, QString name, double expectedMs) {
    CanModel::RuleSpec rule;
    rule.id = id;
    rule.name = name;
    rule.expectedPeriodMs = expectedMs;
    rule.ttlWarnMs = expectedMs * 4.0;
    rule.ttlErrMs = expectedMs * 7.5;
    rule.periodWarnPct = 20.0;
    rule.periodErrPct = 50.0;
    rule.timingEnabled = true;
    return rule;
}

CanModel::SignalSpec baseByteSignal(QString name, int byteIndex1Based) {
    CanModel::SignalSpec sig;
    sig.name = name;
    sig.byteIndex1Based = byteIndex1Based;
    sig.lengthBits = 8;
    sig.startBitLsb = 0;
    sig.scale = 1.0;
    sig.offset = 0.0;
    sig.signedValue = false;
    return sig;
}

CanModel::SignalMessageSpec makeRangeMessage() {
    CanModel::SignalMessageSpec msg;
    msg.id = 0x520;
    msg.name = QStringLiteral("Truth Value Range");
    auto sig = baseByteSignal(QStringLiteral("Motor Temperature"), 1);
    sig.unit = QStringLiteral("C");
    sig.alarmMode = QStringLiteral("range");
    sig.hasWarnMax = true;
    sig.warnMax = 50.0;
    sig.hasErrMax = true;
    sig.errMax = 60.0;
    sig.alarmSeverity = QStringLiteral("ERR");
    sig.alarmMessage = QStringLiteral("Temperature too high");
    msg.signalSpecs = {sig};
    return msg;
}

CanModel::SignalMessageSpec makeReservedMessage() {
    CanModel::SignalMessageSpec msg;
    msg.id = 0x521;
    msg.name = QStringLiteral("Truth Reserved Status");
    auto sig = baseByteSignal(QStringLiteral("Reserved Fault Bit"), 2);
    sig.lengthBits = 1;
    sig.reserved = true;
    sig.alarmMode = QStringLiteral("reserved");
    sig.alarmSeverity = QStringLiteral("ERR");
    sig.alarmMessage = QStringLiteral("Reserved bit set");
    msg.signalSpecs = {sig};
    return msg;
}

AnalysisRuntime::Config truthConfig() {
    AnalysisRuntime::Config config;
    config.modelEnabled = true;
    config.maxStateKeys = 128;
    config.maxRowsPerSnapshot = 128;
    config.rules.insert(0x510, makeRule(0x510, QStringLiteral("Truth Timing 20ms"), 20.0));
    config.rules.insert(0x520, makeRule(0x520, QStringLiteral("Truth Value Range"), 40.0));
    config.rules.insert(0x521, makeRule(0x521, QStringLiteral("Truth Reserved Status"), 40.0));
    config.signalMessages.insert(0x520, makeRangeMessage());
    config.signalMessages.insert(0x521, makeReservedMessage());
    return config;
}

QVariantMap findRow(const QVector<QVariantMap>& rows, const QString& keyPart) {
    for (const QVariantMap& row : rows) {
        const QString key = row.value(QStringLiteral("key")).toString();
        const QString idText = row.value(QStringLiteral("idText")).toString();
        if (key.contains(keyPart, Qt::CaseInsensitive) || idText.contains(keyPart, Qt::CaseInsensitive)) return row;
    }
    return {};
}

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

TypedRecord makeRawRecord(quint16 seq, const FrameRecord& frame) {
    QByteArray payload;
    appendU64(payload, frame.tExtUs);
    appendU32(payload, frame.canId);
    payload.append(char(frame.dlc));
    payload.append(char(frame.bus));
    for (int i = 0; i < 8; ++i) payload.append(char(frame.data[i]));
    appendU32(payload, seq);
    appendU32(payload, 0);

    TypedRecord record;
    record.header.recordType = static_cast<quint8>(TypedRecordType::CanRxRaw);
    record.header.seq = seq;
    record.header.payloadLength = quint16(payload.size());
    record.payload = payload;
    return record;
}

TypedRecord makeSegmentRecord(quint16 seq, const FrameRecordList& frames) {
    QByteArray payload;
    appendU64(payload, seq);
    appendU64(payload, frames.isEmpty() ? 0 : frames.first().captureSeq);
    appendU16(payload, quint16(frames.size()));
    payload.append(char(kTypedCanRxSegmentLegacyEntrySize));
    payload.append(char(0x01));
    appendU32(payload, 0);
    appendU32(payload, 0);
    appendU32(payload, 0);
    for (const FrameRecord& frame : frames) {
        appendU64(payload, frame.hasCaptureSeq ? frame.captureSeq : 0);
        appendU64(payload, frame.tExtUs);
        appendU32(payload, frame.canId);
        payload.append(char(frame.dlc));
        payload.append(char(frame.bus));
        for (int i = 0; i < 8; ++i) payload.append(char(frame.data[i]));
    }

    TypedRecord record;
    record.header.recordType = static_cast<quint8>(TypedRecordType::CanRxSegment);
    record.header.seq = seq;
    record.header.payloadLength = quint16(payload.size());
    record.payload = payload;
    return record;
}

FrameRecord frameFromRawRecord(const TypedRecord& record) {
    const auto raw = decodeTypedCanRaw(record);
    Q_ASSERT(raw.has_value());
    FrameRecord frame;
    frame.tExtUs = raw->monoUs;
    frame.canId = raw->canId;
    frame.ext = raw->extended;
    frame.rtr = raw->rtr;
    frame.dlc = raw->dlc;
    frame.bus = raw->bus;
    std::memcpy(frame.data, raw->data, sizeof(frame.data));
    return frame;
}

FrameRecord frameFromSegmentEntry(const TypedRecord& record, qsizetype index) {
    const auto entry = decodeTypedCanRxSegmentEntry(record, index);
    Q_ASSERT(entry.has_value());
    FrameRecord frame;
    frame.tExtUs = entry->monoUs;
    frame.canId = entry->canId;
    frame.ext = entry->extended;
    frame.rtr = entry->rtr;
    frame.dlc = entry->dlc;
    frame.bus = entry->bus;
    frame.hasCaptureSeq = true;
    frame.captureSeq = entry->captureSeq;
    std::memcpy(frame.data, entry->data, sizeof(frame.data));
    return frame;
}

} // namespace

class AnalysisRuntimeTruthStressTest : public QObject {
    Q_OBJECT

private slots:
    void fixtureTruthCoversTimingValueAlarmAndBusKeys();
    void typedSegmentAnalysisParity();
    void outOfOrderCaptureSeqDoesNotBecomeLoss();
    void captureSeqGapDoesNotBecomePeriodError();
};

void AnalysisRuntimeTruthStressTest::fixtureTruthCoversTimingValueAlarmAndBusKeys() {
    AnalysisRuntime runtime;
    runtime.setConfig(truthConfig());

    runtime.ingestFrame(makeFrame(0, 0x510, 0, 8, {0x01}), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(0, 0x510, 20000, 8, {0x02}), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(0, 0x510, 60000, 4, {0x03}), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(1, 0x510, 0, 8, {0x10}), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(1, 0x510, 20000, 8, {0x11}), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(0, 0x520, 61000, 8, {70}), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(0, 0x521, 62000, 8, {0x00, 0x01}), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(0, 0x7A0, 63000, 8, {0xFF}), QStringLiteral("live"));

    const auto snapshot = runtime.makeSnapshot(64, QStringLiteral("live"));
    QCOMPARE(snapshot.status.acceptedCanRxFrames, quint64(8));
    QCOMPARE(snapshot.status.truthLoss, quint64(0));
    QCOMPARE(snapshot.status.analysisOverrun, quint64(0));
    QCOMPARE(snapshot.status.stateKeyCount, 5);

    const QVariantMap bus0Timing = findRow(snapshot.timingRows, QStringLiteral("BUS0|STD|DATA|0X510"));
    const QVariantMap bus1Timing = findRow(snapshot.timingRows, QStringLiteral("BUS1|STD|DATA|0X510"));
    QVERIFY(!bus0Timing.isEmpty());
    QVERIFY(!bus1Timing.isEmpty());
    QCOMPARE(bus0Timing.value(QStringLiteral("severity")).toString(), QStringLiteral("ERR"));
    QCOMPARE(bus0Timing.value(QStringLiteral("lastGapMsText")).toString(), QStringLiteral("40.0 ms"));
    QCOMPARE(bus1Timing.value(QStringLiteral("lastGapMsText")).toString(), QStringLiteral("20.0 ms"));
    QVERIFY(bus0Timing.value(QStringLiteral("dlcHistogram")).toString().contains(QStringLiteral("4:1")));

    const QVariantMap rangeValue = findRow(snapshot.valueRows, QStringLiteral("0X520"));
    const QVariantMap reservedValue = findRow(snapshot.valueRows, QStringLiteral("0X521"));
    QVERIFY(!rangeValue.isEmpty());
    QVERIFY(!reservedValue.isEmpty());
    QCOMPARE(rangeValue.value(QStringLiteral("severity")).toString(), QStringLiteral("ERR"));
    QVERIFY(rangeValue.value(QStringLiteral("reason")).toString().contains(QStringLiteral("Temperature too high")));
    QCOMPARE(reservedValue.value(QStringLiteral("severity")).toString(), QStringLiteral("ERR"));
    QVERIFY(reservedValue.value(QStringLiteral("reason")).toString().contains(QStringLiteral("Reserved bit set")));

    int timingAlarmRows = 0;
    int valueAlarmRows = 0;
    for (const QVariantMap& row : snapshot.alarmRows) {
        if (row.value(QStringLiteral("category")).toString() == QStringLiteral("timing")) ++timingAlarmRows;
        if (row.value(QStringLiteral("category")).toString() == QStringLiteral("value")) ++valueAlarmRows;
    }
    QVERIFY(timingAlarmRows >= 1);
    QVERIFY(valueAlarmRows >= 2);
}

void AnalysisRuntimeTruthStressTest::typedSegmentAnalysisParity() {
    FrameRecordList frames;
    FrameRecord a = makeFrame(0, 0x510, 0, 8, {0x01});
    a.hasCaptureSeq = true;
    a.captureSeq = 100;
    FrameRecord b = makeFrame(0, 0x510, 40000, 4, {0x02});
    b.hasCaptureSeq = true;
    b.captureSeq = 101;
    FrameRecord c = makeFrame(0, 0x520, 41000, 8, {70});
    c.hasCaptureSeq = true;
    c.captureSeq = 102;
    frames << a << b << c;

    AnalysisRuntime rawRuntime;
    AnalysisRuntime segmentRuntime;
    rawRuntime.setConfig(truthConfig());
    segmentRuntime.setConfig(truthConfig());

    quint16 seq = 1;
    for (const FrameRecord& frame : frames) rawRuntime.ingestFrame(frameFromRawRecord(makeRawRecord(seq++, frame)), QStringLiteral("live"));
    const TypedRecord segment = makeSegmentRecord(77, frames);
    const auto header = decodeTypedCanRxSegmentHeader(segment);
    QVERIFY(header.has_value());
    QCOMPARE(header->frameCount, quint16(frames.size()));
    for (qsizetype index = 0; index < header->frameCount; ++index) {
        segmentRuntime.ingestFrame(frameFromSegmentEntry(segment, index), QStringLiteral("live"));
    }

    const auto rawSnapshot = rawRuntime.makeSnapshot(42, QStringLiteral("live"));
    const auto segmentSnapshot = segmentRuntime.makeSnapshot(42, QStringLiteral("live"));
    QCOMPARE(segmentSnapshot.status.acceptedCanRxFrames, rawSnapshot.status.acceptedCanRxFrames);
    QCOMPARE(segmentSnapshot.status.truthLoss, rawSnapshot.status.truthLoss);
    QCOMPARE(segmentSnapshot.summary.value(QStringLiteral("level")).toString(),
             rawSnapshot.summary.value(QStringLiteral("level")).toString());
    QCOMPARE(segmentSnapshot.timingRows.size(), rawSnapshot.timingRows.size());
    QCOMPARE(segmentSnapshot.valueRows.size(), rawSnapshot.valueRows.size());

    const QVariantMap rawTiming = findRow(rawSnapshot.timingRows, QStringLiteral("0X510"));
    const QVariantMap segmentTiming = findRow(segmentSnapshot.timingRows, QStringLiteral("0X510"));
    QCOMPARE(segmentTiming.value(QStringLiteral("lastGapMsText")).toString(),
             rawTiming.value(QStringLiteral("lastGapMsText")).toString());
    QCOMPARE(segmentTiming.value(QStringLiteral("dlcHistogram")).toString(),
             rawTiming.value(QStringLiteral("dlcHistogram")).toString());
}

void AnalysisRuntimeTruthStressTest::outOfOrderCaptureSeqDoesNotBecomeLoss() {
    AnalysisRuntime runtime;
    AnalysisRuntime::Config config;
    config.maxStateKeys = 16;
    config.maxRowsPerSnapshot = 16;
    runtime.setConfig(config);

    FrameRecord a = makeFrame(0, 0x510, 0, 8, {0x01});
    a.hasCaptureSeq = true;
    a.captureSeq = 100;
    FrameRecord c = makeFrame(1, 0x520, 40000, 8, {0x03});
    c.hasCaptureSeq = true;
    c.captureSeq = 102;
    FrameRecord b = makeFrame(0, 0x511, 20000, 8, {0x02});
    b.hasCaptureSeq = true;
    b.captureSeq = 101;

    runtime.ingestFrame(a, QStringLiteral("live"));
    runtime.ingestFrame(c, QStringLiteral("live"));
    runtime.ingestFrame(b, QStringLiteral("live"));

    const auto snapshot = runtime.makeSnapshot(40, QStringLiteral("live"));
    QCOMPARE(snapshot.status.acceptedCanRxFrames, quint64(3));
    QCOMPARE(snapshot.status.captureSeqGapEvents, quint64(0));
    QCOMPARE(snapshot.status.transportContaminatedIntervals, quint64(0));
    QCOMPARE(snapshot.summary.value(QStringLiteral("level")).toString(), QStringLiteral("OK"));

    const QVariantMap row = findRow(snapshot.timingRows, QStringLiteral("0X510"));
    QVERIFY(!row.isEmpty());
    QVERIFY(!row.value(QStringLiteral("transportContaminated")).toBool());
}

void AnalysisRuntimeTruthStressTest::captureSeqGapDoesNotBecomePeriodError() {
    AnalysisRuntime runtime;
    AnalysisRuntime::Config config;
    config.modelEnabled = true;
    config.maxStateKeys = 16;
    config.maxRowsPerSnapshot = 16;
    config.rules.insert(0x510, makeRule(0x510, QStringLiteral("Truth Timing 20ms"), 20.0));
    runtime.setConfig(config);

    FrameRecord a = makeFrame(0, 0x510, 0, 8, {0x01});
    a.hasCaptureSeq = true;
    a.captureSeq = 100;
    FrameRecord b = makeFrame(0, 0x510, 92000000ULL, 8, {0x02});
    b.hasCaptureSeq = true;
    b.captureSeq = 900000;

    runtime.ingestFrame(a, QStringLiteral("live"));
    runtime.ingestFrame(b, QStringLiteral("live"));

    const auto snapshot = runtime.makeSnapshot(92000, QStringLiteral("live"));
    QCOMPARE(snapshot.status.acceptedCanRxFrames, quint64(2));
    QCOMPARE(snapshot.status.captureSeqGapEvents, quint64(1));
    QCOMPARE(snapshot.status.transportContaminatedIntervals, quint64(1));
    QCOMPARE(snapshot.summary.value(QStringLiteral("level")).toString(), QStringLiteral("WARN"));

    const QVariantMap row = findRow(snapshot.timingRows, QStringLiteral("0X510"));
    QVERIFY(!row.isEmpty());
    QVERIFY(row.value(QStringLiteral("transportContaminated")).toBool());
    QCOMPARE(row.value(QStringLiteral("transportGapCount")).toULongLong(), qulonglong(1));
    QVERIFY(row.value(QStringLiteral("transportGapText")).toString().contains(QStringLiteral("92000")));
    QVERIFY(row.value(QStringLiteral("reason")).toString().contains(QStringLiteral("capture_seq")));
    QVERIFY(row.value(QStringLiteral("severity")).toString() != QStringLiteral("ERR"));
}

QTEST_MAIN(AnalysisRuntimeTruthStressTest)
#include "test_analysis_runtime_truth_stress.moc"
