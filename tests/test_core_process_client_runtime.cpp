#include "core/CoreProcessClientRuntime.h"

#include <QSignalSpy>
#include <QTest>

class CoreProcessClientRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void launchesCoreProcessAndQueriesViews() {
        CanMonitorCore::CoreProcessClientRuntime runtime;
        QSignalSpy stateSpy(&runtime, &CanMonitorCore::CoreProcessClientRuntime::stateChanged);
        QSignalSpy snapshotSpy(&runtime, &CanMonitorCore::CoreProcessClientRuntime::viewSnapshotReady);
        QSignalSpy errorSpy(&runtime, &CanMonitorCore::CoreProcessClientRuntime::errorOccurred);

        QString error;
        QVERIFY2(runtime.startServerOnly(QStringLiteral(CAN_MONITOR_CORE_PROCESS_EXE), &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.isIpcConnected(), 5000);
        QVERIFY(runtime.isActive());

        CanMonitorCore::CoreViewClientRuntime::ViewRequest request;
        request.valid = true;
        request.viewName = QStringLiteral("core_health");
        request.requestId = 9001;
        request.limit = 1;
        QVERIFY(runtime.requestView(request));

        QTRY_VERIFY_WITH_TIMEOUT(snapshotSpy.size() >= 1, 5000);
        bool foundHealth = false;
        for (const auto& entry : snapshotSpy) {
            if (entry.at(0).toULongLong() != request.requestId) continue;
            foundHealth = true;
            QCOMPARE(entry.at(1).toBool(), true);
            const QJsonObject snapshot = entry.at(2).toJsonObject();
            QCOMPARE(snapshot.value(QStringLiteral("payload")).toObject().value(QStringLiteral("process")).toString(),
                     QStringLiteral("vsm-capture-core"));
        }
        QVERIFY(foundHealth);
        QVERIFY(!stateSpy.isEmpty());
        QVERIFY(errorSpy.isEmpty());
        runtime.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.isActive(), 3000);
    }

    void rejectsInvalidGatewayEndpoint() {
        CanMonitorCore::CoreProcessClientRuntime runtime;
        QString error;
        QVERIFY(!runtime.startGatewayTcp(QStringLiteral(CAN_MONITOR_CORE_PROCESS_EXE),
                                         QStringLiteral("COM7"),
                                         &error));
        QVERIFY(error.contains(QStringLiteral("gateway endpoint")));
        QVERIFY(!runtime.isActive());
    }

    void launchesGatewayModeAndKeepsIpcAlive() {
        CanMonitorCore::CoreProcessClientRuntime runtime;
        QSignalSpy snapshotSpy(&runtime, &CanMonitorCore::CoreProcessClientRuntime::viewSnapshotReady);

        QString error;
        QVERIFY2(runtime.startGatewayTcp(QStringLiteral(CAN_MONITOR_CORE_PROCESS_EXE),
                                         QStringLiteral("tcp://127.0.0.1:9"),
                                         &error),
                 qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.isIpcConnected(), 5000);
        QVERIFY(runtime.isActive());

        CanMonitorCore::CoreViewClientRuntime::ViewRequest request;
        request.valid = true;
        request.viewName = QStringLiteral("transport_summary");
        request.requestId = 9002;
        request.limit = 1;
        QVERIFY(runtime.requestView(request));

        QTRY_VERIFY_WITH_TIMEOUT(snapshotSpy.size() >= 1, 5000);
        bool foundTransport = false;
        for (const auto& entry : snapshotSpy) {
            if (entry.at(0).toULongLong() != request.requestId) continue;
            foundTransport = true;
            const QJsonObject payload = entry.at(2).toJsonObject().value(QStringLiteral("payload")).toObject();
            QCOMPARE(payload.value(QStringLiteral("serial_owner")).toString(), QStringLiteral("core"));
        }
        QVERIFY(foundTransport);
        runtime.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.isActive(), 3000);
    }
};

QTEST_MAIN(CoreProcessClientRuntimeTest)
#include "test_core_process_client_runtime.moc"
