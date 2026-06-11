#include "control/ControlRuntime.h"

#include <QtTest/QtTest>

class ControlRuntimeFoundationTest : public QObject {
    Q_OBJECT

private slots:
    void clampsTargetAndIntent() {
        CanMonitorControl::ControlRuntime runtime;

        runtime.setTargetBus(300, true);
        runtime.setTargetRpm(20000);
        runtime.setTargetSteeringDeg(-120.0);

        QCOMPARE(runtime.target().bus, 255);
        QCOMPARE(runtime.target().rpm, 10000);
        QCOMPARE(runtime.target().steeringDeg, -90.0);
        QVERIFY(runtime.targetBusManualOverride());

        runtime.setCurrentIntent(-20000, 20000, 120.0, 2, 7);
        const auto reverseIntent = runtime.currentIntent();
        QCOMPARE(reverseIntent.signedCommand, -10000);
        QCOMPARE(reverseIntent.rpm, 10000);
        QCOMPARE(reverseIntent.steeringDeg, 90.0);
        QCOMPARE(reverseIntent.motorMode, quint8(2));
        QCOMPARE(reverseIntent.drivingMode, quint8(7));

        runtime.setCurrentIntent(50, 60, 7.5, 9, 4);
        QCOMPARE(runtime.currentIntent().motorMode, quint8(1));
    }

    void tracksCountersAndOperatorSummary() {
        CanMonitorControl::ControlRuntime runtime;

        QCOMPARE(runtime.nextCommandId(), quint32(1));
        QCOMPARE(runtime.nextCommandId(), quint32(2));
        QCOMPARE(runtime.nextAliveCounter(), quint8(0));
        QCOMPARE(runtime.nextAliveCounter(), quint8(1));

        runtime.setArmed(true);
        runtime.setTestRunning(true);
        runtime.setTargetBus(1, false);
        runtime.setTargetRpm(900);
        runtime.setTargetSteeringDeg(12.5);
        runtime.setStatusText(QStringLiteral("latched"));

        const QString summary = runtime.statusSummary(QStringLiteral("typed"), true, false, QStringLiteral("board stale"));
        QVERIFY(summary.contains(QStringLiteral("제어 ARM")));
        QVERIFY(summary.contains(QStringLiteral("테스트 실행 중")));
        QVERIFY(summary.contains(QStringLiteral("BUS 1")));
        QVERIFY(summary.contains(QStringLiteral("보드 gate: board stale")));
        QVERIFY(summary.contains(QStringLiteral("latched")));

        const QString verdict = runtime.actionVerdict(QStringLiteral("typed"),
                                                       true,
                                                       true,
                                                       true,
                                                       true,
                                                       false,
                                                       false,
                                                       QStringLiteral("ready"));
        QVERIFY(verdict.contains(QStringLiteral("CAN_TX_RAW")));

        const QVariantList checklist = runtime.operatorChecklist(QStringLiteral("typed"),
                                                                 true,
                                                                 true,
                                                                 true,
                                                                 true,
                                                                 true,
                                                                 false,
                                                                 QStringLiteral("ready"));
        QCOMPARE(checklist.size(), 7);
        QCOMPARE(checklist.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("mode"));
        QCOMPARE(checklist.at(5).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("tx"));
        QCOMPARE(checklist.at(5).toMap().value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(checklist.at(6).toMap().value(QStringLiteral("blocking")).toBool(), false);
    }

    void resetsPatternTimingWithoutDroppingLatch() {
        CanMonitorControl::ControlRuntime runtime;
        runtime.setTestRunning(true);
        runtime.setLastBurstWallMs(1234);
        runtime.setLastHeartbeatWallMs(100);
        runtime.setLastLeaseRenewWallMs(200);
        runtime.setCurrentIntent(100, 100, 3.0, 1, 2);

        runtime.resetPatternState();

        QVERIFY(!runtime.testRunning());
        QCOMPARE(runtime.lastBurstWallMs(), qint64(0));
        QCOMPARE(runtime.lastHeartbeatWallMs(), qint64(100));
        QCOMPARE(runtime.lastLeaseRenewWallMs(), qint64(200));
        QCOMPARE(runtime.currentIntent().signedCommand, 100);
    }
};

QTEST_GUILESS_MAIN(ControlRuntimeFoundationTest)
#include "test_control_runtime_foundation.moc"
