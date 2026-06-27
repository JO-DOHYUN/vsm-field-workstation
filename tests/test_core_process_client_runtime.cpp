#include "core/CoreProcessClientRuntime.h"

#include <QSignalSpy>
#include <QTemporaryDir>
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

    void startsGatewayViaIpcOnExistingCoreProcess() {
        CanMonitorCore::CoreProcessClientRuntime runtime;
        QSignalSpy snapshotSpy(&runtime, &CanMonitorCore::CoreProcessClientRuntime::viewSnapshotReady);

        QString error;
        QVERIFY2(runtime.startServerOnly(QStringLiteral(CAN_MONITOR_CORE_PROCESS_EXE), &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.isIpcConnected(), 5000);
        const QString serverName = runtime.serverName();

        QVERIFY2(runtime.startGatewayTcp(QStringLiteral(CAN_MONITOR_CORE_PROCESS_EXE),
                                         QStringLiteral("tcp://127.0.0.1:9"),
                                         &error),
                 qPrintable(error));
        QCOMPARE(runtime.serverName(), serverName);
        QVERIFY(runtime.isActive());
        QVERIFY(runtime.isIpcConnected());

        CanMonitorCore::CoreViewClientRuntime::ViewRequest request;
        request.valid = true;
        request.viewName = QStringLiteral("transport_summary");
        request.requestId = 9003;
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

    void reportsHostFrameFailureWhenCoreTransportIsNotStarted() {
        CanMonitorCore::CoreProcessClientRuntime runtime;
        QSignalSpy writeSpy(&runtime, &CanMonitorCore::CoreProcessClientRuntime::hostFrameWriteResult);

        QString error;
        QVERIFY2(runtime.startServerOnly(QStringLiteral(CAN_MONITOR_CORE_PROCESS_EXE), &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.isIpcConnected(), 5000);

        const QByteArray frame = QByteArray::fromHex("a55a010b000100000000");
        QVERIFY(runtime.sendHostFrame(frame, QStringLiteral("unit host frame"), &error));
        QTRY_COMPARE_WITH_TIMEOUT(writeSpy.size(), 1, 5000);
        const auto args = writeSpy.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(args.at(1).toString().contains(QStringLiteral("core transport not connected")));
        QCOMPARE(args.at(2).toULongLong(), quint64(0));

        runtime.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.isActive(), 3000);
    }

    void startsAndStopsCoreCaptureStorage() {
        CanMonitorCore::CoreProcessClientRuntime runtime;
        QSignalSpy storageSpy(&runtime, &CanMonitorCore::CoreProcessClientRuntime::captureStorageUpdate);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString error;
        QVERIFY2(runtime.startServerOnly(QStringLiteral(CAN_MONITOR_CORE_PROCESS_EXE), &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.isIpcConnected(), 5000);

        QVERIFY(runtime.startCapture(dir.path(), QJsonObject{{QStringLiteral("source"), QStringLiteral("unit-core-process")}}, &error));
        QTRY_VERIFY_WITH_TIMEOUT(storageSpy.size() >= 1, 5000);
        bool activeSeen = false;
        for (const auto& entry : storageSpy) {
            if (entry.at(1).toString().isEmpty() && entry.at(2).toBool() && entry.at(3).toBool()) {
                activeSeen = true;
            }
        }
        QVERIFY(activeSeen);

        QVERIFY(runtime.stopCapture(dir.path(), QJsonObject{{QStringLiteral("reason"), QStringLiteral("unit-stop")}}, &error));
        QTRY_VERIFY_WITH_TIMEOUT(storageSpy.size() >= 2, 5000);
        bool finalizedSeen = false;
        for (const auto& entry : storageSpy) {
            if (entry.at(2).toBool() && !entry.at(3).toBool()) {
                finalizedSeen = true;
            }
        }
        QVERIFY(finalizedSeen);

        runtime.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.isActive(), 3000);
    }
};

QTEST_MAIN(CoreProcessClientRuntimeTest)
#include "test_core_process_client_runtime.moc"
