#include "perf/LiveRuntimeTrace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

class LiveRuntimeTraceTest : public QObject {
    Q_OBJECT

private slots:
    void signalCountersTrackInflightAndSlotDelay() {
        auto& registry = CanMonitorPerf::LiveRuntimeTraceRegistry::instance();
        registry.reset();
        registry.setEnabled(true);

        registry.noteEmit(CanMonitorPerf::LiveTraceSignal::typedLiveLatestStatusChanged, 1, 96);
        auto snapshots = registry.signalSnapshots();
        const auto truthIt = std::find_if(snapshots.cbegin(), snapshots.cend(), [](const auto& s) {
            return s.name == QStringLiteral("typedLiveLatestStatusChanged");
        });
        QVERIFY(truthIt != snapshots.cend());
        QCOMPARE(truthIt->emitSeq, quint64(1));
        QCOMPARE(truthIt->slotSeq, quint64(0));
        QCOMPARE(truthIt->inflight, quint64(1));

        QTest::qWait(2);
        registry.noteSlot(CanMonitorPerf::LiveTraceSignal::typedLiveLatestStatusChanged, 1, 96);
        snapshots = registry.signalSnapshots();
        const auto truthAfter = std::find_if(snapshots.cbegin(), snapshots.cend(), [](const auto& s) {
            return s.name == QStringLiteral("typedLiveLatestStatusChanged");
        });
        QVERIFY(truthAfter != snapshots.cend());
        QCOMPARE(truthAfter->emitSeq, quint64(1));
        QCOMPARE(truthAfter->slotSeq, quint64(1));
        QCOMPARE(truthAfter->inflight, quint64(0));
        QVERIFY(truthAfter->maxSlotDelayMs >= 0);
        registry.setEnabled(false);
    }

    void writerCreatesBoundedSnapshotArtifacts() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        CanMonitorPerf::RuntimeOwnerSnapshot owner;
        owner.liveModelRows = 3;
        owner.rawLedgerRows = 7;
        owner.pendingLiveRows = 2;

        CanMonitorPerf::LiveRuntimeTraceService service;
        service.startSession(dir.path(), qApp, true);
        service.updateOwnerSnapshot(owner);
        QTest::qWait(750);
        service.flush();
        service.stopSession(QStringLiteral("test"));

        const QString tracePath = QDir(dir.path()).filePath(QStringLiteral("live_runtime_trace.jsonl"));
        const QString metricsPath = QDir(dir.path()).filePath(QStringLiteral("process_metrics.csv"));
        QVERIFY2(QFileInfo::exists(tracePath), qPrintable(tracePath));
        QVERIFY2(QFileInfo(tracePath).size() > 0, qPrintable(tracePath));
        QVERIFY2(QFileInfo::exists(metricsPath), qPrintable(metricsPath));
        QVERIFY2(QFileInfo(metricsPath).size() > 0, qPrintable(metricsPath));
    }
};

QTEST_MAIN(LiveRuntimeTraceTest)
#include "test_live_runtime_trace.moc"
