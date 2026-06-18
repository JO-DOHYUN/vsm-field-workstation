#include "analysis/AnalysisRuntime.h"

#include <QtTest/QtTest>

#include <cstring>

using CanMonitorAnalysis::AnalysisRuntime;

namespace {

FrameRecord makeFrame(quint8 bus, quint32 id, quint64 us, quint8 dlc, quint8 seed = 0) {
    FrameRecord frame;
    frame.bus = bus;
    frame.canId = id;
    frame.tExtUs = us;
    frame.dlc = dlc;
    for (int i = 0; i < 8; ++i) frame.data[i] = quint8(seed + i);
    return frame;
}

CanModel::RuleSpec makeRule(quint32 id, double expectedMs = 20.0) {
    CanModel::RuleSpec rule;
    rule.id = id;
    rule.name = QStringLiteral("Rule %1").arg(id);
    rule.expectedPeriodMs = expectedMs;
    rule.periodWarnPct = 30.0;
    rule.periodErrPct = 80.0;
    rule.ttlWarnMs = expectedMs * 3.0;
    rule.ttlErrMs = expectedMs * 5.0;
    rule.timingEnabled = true;
    return rule;
}

} // namespace

class AnalysisRuntimeFoundationTest : public QObject {
    Q_OBJECT

private slots:
    void truthConsumesAllBusSeparated();
    void timingGapAndDlcStats();
    void overrunIsDiagnosticNotSilent();
    void snapshotDiffTracksBoundedChanges();
};

void AnalysisRuntimeFoundationTest::truthConsumesAllBusSeparated() {
    AnalysisRuntime runtime;
    AnalysisRuntime::Config config;
    config.modelEnabled = true;
    config.rules.insert(0x123, makeRule(0x123));
    runtime.setConfig(config);

    runtime.ingestFrame(makeFrame(0, 0x123, 1000, 3, 1), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(1, 0x123, 2000, 8, 2), QStringLiteral("live"));

    const auto snapshot = runtime.makeSnapshot(2, QStringLiteral("live"));
    QCOMPARE(snapshot.status.acceptedCanRxFrames, quint64(2));
    QCOMPARE(snapshot.status.truthLoss, quint64(0));
    QCOMPARE(snapshot.status.stateKeyCount, 2);
    QCOMPARE(snapshot.valueRows.size(), 2);

    QSet<QString> keys;
    QSet<int> dlcs;
    for (const auto& row : snapshot.valueRows) {
        keys.insert(row.value(QStringLiteral("key")).toString());
        dlcs.insert(row.value(QStringLiteral("dlc")).toInt());
    }
    QVERIFY(keys.contains(QStringLiteral("BUS0|STD|DATA|0X123")));
    QVERIFY(keys.contains(QStringLiteral("BUS1|STD|DATA|0X123")));
    QVERIFY(dlcs.contains(3));
    QVERIFY(dlcs.contains(8));
}

void AnalysisRuntimeFoundationTest::timingGapAndDlcStats() {
    AnalysisRuntime runtime;
    AnalysisRuntime::Config config;
    config.modelEnabled = true;
    config.rules.insert(0x220, makeRule(0x220, 20.0));
    runtime.setConfig(config);

    runtime.ingestFrame(makeFrame(0, 0x220, 0, 8, 0), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(0, 0x220, 20000, 4, 1), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(0, 0x220, 60000, 8, 2), QStringLiteral("live"));

    const auto snapshot = runtime.makeSnapshot(60, QStringLiteral("live"));
    QCOMPARE(snapshot.status.acceptedCanRxFrames, quint64(3));
    QCOMPARE(snapshot.status.stateKeyCount, 1);
    QCOMPARE(snapshot.timingRows.size(), 1);
    const QVariantMap row = snapshot.timingRows.first();
    QCOMPARE(row.value(QStringLiteral("lastGapMsText")).toString(), QStringLiteral("40.0 ms"));
    QCOMPARE(row.value(QStringLiteral("minGapMsText")).toString(), QStringLiteral("20.0 ms"));
    QCOMPARE(row.value(QStringLiteral("maxGapMsText")).toString(), QStringLiteral("40.0 ms"));
    QVERIFY(row.value(QStringLiteral("dlcHistogram")).toString().contains(QStringLiteral("4:1")));
    QVERIFY(row.value(QStringLiteral("dlcHistogram")).toString().contains(QStringLiteral("8:2")));
}

void AnalysisRuntimeFoundationTest::overrunIsDiagnosticNotSilent() {
    AnalysisRuntime runtime;
    AnalysisRuntime::Config config;
    config.maxStateKeys = 1;
    runtime.setConfig(config);

    runtime.ingestFrame(makeFrame(0, 0x100, 1000, 8, 0), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(0, 0x101, 2000, 8, 0), QStringLiteral("live"));

    const auto snapshot = runtime.makeSnapshot(2, QStringLiteral("live"));
    QCOMPARE(snapshot.status.acceptedCanRxFrames, quint64(2));
    QCOMPARE(snapshot.status.stateKeyCount, 1);
    QCOMPARE(snapshot.status.truthLoss, quint64(1));
    QCOMPARE(snapshot.status.analysisOverrun, quint64(1));
    QCOMPARE(snapshot.summary.value(QStringLiteral("level")).toString(), QStringLiteral("ERR"));
}

void AnalysisRuntimeFoundationTest::snapshotDiffTracksBoundedChanges() {
    AnalysisRuntime runtime;
    runtime.setConfig({});

    runtime.ingestFrame(makeFrame(0, 0x321, 1000, 8, 1), QStringLiteral("live"));
    const auto before = runtime.makeSnapshot(1, QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(0, 0x321, 2000, 8, 2), QStringLiteral("live"));
    runtime.ingestFrame(makeFrame(1, 0x322, 3000, 8, 3), QStringLiteral("live"));
    const auto after = runtime.makeSnapshot(3, QStringLiteral("live"));

    const auto diff = AnalysisRuntime::diff(before, after);
    QCOMPARE(diff.fromSeq, before.seq);
    QCOMPARE(diff.toSeq, after.seq);
    QVERIFY(diff.changedKeys.contains(QStringLiteral("BUS0|STD|DATA|0X321")));
    QVERIFY(diff.insertedKeys.contains(QStringLiteral("BUS1|STD|DATA|0X322")));
    QVERIFY(diff.removedKeys.isEmpty());
    QVERIFY(diff.summaryChanged);
}

QTEST_MAIN(AnalysisRuntimeFoundationTest)
#include "test_analysis_runtime_foundation.moc"
