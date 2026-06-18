#include "perf/PerformanceProbeRuntime.h"

#include <QtTest/QtTest>

class PerformanceProbeRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void disabledIsNoop();
    void enabledRecordsBoundedStats();
};

void PerformanceProbeRuntimeTest::disabledIsNoop() {
    CanMonitorPerf::PerformanceProbeRuntime::reset();
    CanMonitorPerf::PerformanceProbeRuntime::setEnabled(false);
    CanMonitorPerf::PerformanceProbeRuntime::record("disabled.sample", 500, 1, 100);
    QCOMPARE(CanMonitorPerf::PerformanceProbeRuntime::rows().size(), 0);
    QVERIFY(!CanMonitorPerf::PerformanceProbeRuntime::snapshot().value(QStringLiteral("enabled")).toBool());
}

void PerformanceProbeRuntimeTest::enabledRecordsBoundedStats() {
    CanMonitorPerf::PerformanceProbeRuntime::reset();
    CanMonitorPerf::PerformanceProbeRuntime::setEnabled(true);
    CanMonitorPerf::PerformanceProbeRuntime::record("analysis.ingest_records", 100, 7, 200);
    CanMonitorPerf::PerformanceProbeRuntime::record("analysis.ingest_records", 350, 9, 200);
    CanMonitorPerf::PerformanceProbeRuntime::record("graph.flush_refresh", 50, 2, 200);

    const QVariantList rows = CanMonitorPerf::PerformanceProbeRuntime::rows();
    QVERIFY(rows.size() >= 2);
    const QVariantMap first = rows.first().toMap();
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("analysis.ingest_records"));
    QCOMPARE(first.value(QStringLiteral("calls")).toString(), QStringLiteral("2"));
    QCOMPARE(first.value(QStringLiteral("maxUs")).toLongLong(), qint64(350));
    QCOMPARE(first.value(QStringLiteral("lastBacklog")).toLongLong(), qint64(9));
    QCOMPARE(first.value(QStringLiteral("overBudget")).toString(), QStringLiteral("1"));

    const QJsonObject snapshot = CanMonitorPerf::PerformanceProbeRuntime::snapshot();
    QVERIFY(snapshot.value(QStringLiteral("enabled")).toBool());
    QCOMPARE(snapshot.value(QStringLiteral("metric_count")).toInt(), rows.size());
    CanMonitorPerf::PerformanceProbeRuntime::setEnabled(false);
}

QTEST_MAIN(PerformanceProbeRuntimeTest)
#include "test_performance_probe_runtime.moc"
