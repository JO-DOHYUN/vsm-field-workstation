#include "core/CoreIpcClientRuntime.h"
#include "core/CoreIpcServerRuntime.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

class CoreIpcRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void clientQueriesServerViews() {
        CanMonitorCore::CoreMaterializedViewStore store;
        QJsonArray frames;
        frames.append(QJsonObject{{QStringLiteral("can_id"), 0x111}, {QStringLiteral("bus"), 0}});
        frames.append(QJsonObject{{QStringLiteral("can_id"), 0x222}, {QStringLiteral("bus"), 1}});
        const auto changed = store.updateArrayView(CanMonitorCore::CoreViewName::LiveLatest,
                                                   QStringLiteral("frames"),
                                                   frames,
                                                   CanMonitorCore::CoreViewSeverity::Ok,
                                                   QJsonObject{{QStringLiteral("key_count"), 2}});

        const QString serverName = QStringLiteral("vsm-core-ipc-test-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(reinterpret_cast<quintptr>(this));
        CanMonitorCore::CoreIpcServerRuntime server(&store);
        QString error;
        QVERIFY2(server.listen(serverName, &error), qPrintable(error));

        CanMonitorCore::CoreIpcClientRuntime client;
        QSignalSpy connectedSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::connectedChanged);
        QSignalSpy pongSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::pongReceived);
        QSignalSpy snapshotSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::viewSnapshotReceived);
        QSignalSpy viewChangedSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::viewChanged);

        client.connectToServer(serverName);
        QTRY_VERIFY(client.isConnected());
        QVERIFY(!connectedSpy.isEmpty());

        const quint64 pingId = client.ping();
        QTRY_COMPARE(pongSpy.size(), 1);
        QCOMPARE(pongSpy.takeFirst().at(0).toULongLong(), pingId);

        const quint64 requestId = client.requestView(QStringLiteral("live_latest"), 0, 1);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        const auto args = snapshotSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), requestId);
        QCOMPARE(args.at(1).toBool(), true);
        const QJsonObject snapshot = args.at(2).toJsonObject();
        QCOMPARE(snapshot.value(QStringLiteral("view_seq")).toString().toULongLong(), changed.viewSeq);
        const QJsonArray limitedFrames = snapshot.value(QStringLiteral("payload")).toObject().value(QStringLiteral("frames")).toArray();
        QCOMPARE(limitedFrames.size(), 1);
        QCOMPARE(limitedFrames.first().toObject().value(QStringLiteral("can_id")).toInt(), 0x222);

        server.publishViewChanged(changed);
        QTRY_COMPARE(viewChangedSpy.size(), 1);
        QCOMPARE(viewChangedSpy.takeFirst().at(0).toJsonObject().value(QStringLiteral("view_name")).toString(),
                 QStringLiteral("live_latest"));

        client.disconnectFromServer();
        server.close();
    }

    void viewChangedNotificationsAreCadencedPerView() {
        CanMonitorCore::CoreMaterializedViewStore store;
        const QString serverName = QStringLiteral("vsm-core-ipc-cadence-test-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(reinterpret_cast<quintptr>(this));
        CanMonitorCore::CoreIpcServerRuntime server(&store);
        QString error;
        QVERIFY2(server.listen(serverName, &error), qPrintable(error));

        CanMonitorCore::CoreIpcClientRuntime client;
        QSignalSpy viewChangedSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::viewChanged);
        client.connectToServer(serverName);
        QTRY_VERIFY(client.isConnected());

        server.publishViewChanged(store.updateView(CanMonitorCore::CoreViewName::TransportSummary,
                                                   QJsonObject{{QStringLiteral("seq"), 1}}));
        QTRY_COMPARE(viewChangedSpy.size(), 1);
        viewChangedSpy.clear();

        for (int seq = 2; seq <= 5; ++seq) {
            server.publishViewChanged(store.updateView(CanMonitorCore::CoreViewName::TransportSummary,
                                                       QJsonObject{{QStringLiteral("seq"), seq}}));
        }

        QTest::qWait(100);
        QCOMPARE(viewChangedSpy.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(viewChangedSpy.size(), 1, 500);
        const QJsonObject changed = viewChangedSpy.takeFirst().at(0).toJsonObject();
        QCOMPARE(changed.value(QStringLiteral("view_name")).toString(), QStringLiteral("transport_summary"));
        QCOMPARE(changed.value(QStringLiteral("view_seq")).toString().toULongLong(), quint64(5));

        const QJsonObject status = server.statusJson();
        QVERIFY(status.value(QStringLiteral("ipc_view_coalesced")).toString().toULongLong() >= 3);
        QVERIFY(status.value(QStringLiteral("ipc_view_deferred")).toString().toULongLong() > 0);

        client.disconnectFromServer();
        server.close();
    }

    void clientSubmitsHostFrameAndReceivesWriteResult() {
        CanMonitorCore::CoreMaterializedViewStore store;
        const QString serverName = QStringLiteral("vsm-core-ipc-host-frame-test-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(reinterpret_cast<quintptr>(this));
        CanMonitorCore::CoreIpcServerRuntime server(&store);
        QString error;
        QVERIFY2(server.listen(serverName, &error), qPrintable(error));

        CanMonitorCore::CoreIpcClientRuntime client;
        QSignalSpy requestSpy(&server, &CanMonitorCore::CoreIpcServerRuntime::hostFrameRequested);
        QSignalSpy resultSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::hostFrameWriteResult);

        client.connectToServer(serverName);
        QTRY_VERIFY(client.isConnected());

        const QByteArray frame = QByteArray::fromHex("a55a010b000100000000");
        const quint64 requestId = client.sendHostFrame(frame, QStringLiteral("unit host frame"));
        QTRY_COMPARE(requestSpy.size(), 1);
        const auto requestArgs = requestSpy.takeFirst();
        QCOMPARE(requestArgs.at(0).toULongLong(), requestId);
        QCOMPARE(requestArgs.at(1).toByteArray(), frame);
        QCOMPARE(requestArgs.at(2).toString(), QStringLiteral("unit host frame"));

        server.publishHostFrameWriteResult(requestId, true, QStringLiteral("unit host frame"), quint64(frame.size()));
        QTRY_COMPARE(resultSpy.size(), 1);
        const auto resultArgs = resultSpy.takeFirst();
        QCOMPARE(resultArgs.at(0).toULongLong(), requestId);
        QCOMPARE(resultArgs.at(1).toBool(), true);
        QCOMPARE(resultArgs.at(2).toString(), QStringLiteral("unit host frame"));
        QCOMPARE(resultArgs.at(3).toULongLong(), quint64(frame.size()));

        client.disconnectFromServer();
        server.close();
    }

    void clientRequestsTransportStartStop() {
        CanMonitorCore::CoreMaterializedViewStore store;
        const QString serverName = QStringLiteral("vsm-core-ipc-transport-test-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(reinterpret_cast<quintptr>(this));
        CanMonitorCore::CoreIpcServerRuntime server(&store);
        QString error;
        QVERIFY2(server.listen(serverName, &error), qPrintable(error));

        CanMonitorCore::CoreIpcClientRuntime client;
        QSignalSpy startSpy(&server, &CanMonitorCore::CoreIpcServerRuntime::transportStartRequested);
        QSignalSpy stopSpy(&server, &CanMonitorCore::CoreIpcServerRuntime::transportStopRequested);

        client.connectToServer(serverName);
        QTRY_VERIFY(client.isConnected());

        const quint64 startId = client.startTransport(QStringLiteral("serial"), QStringLiteral("COM7"));
        QTRY_COMPARE(startSpy.size(), 1);
        auto args = startSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), startId);
        QCOMPARE(args.at(1).toString(), QStringLiteral("serial"));
        QCOMPARE(args.at(2).toString(), QStringLiteral("COM7"));

        const quint64 stopId = client.stopTransport();
        QTRY_COMPARE(stopSpy.size(), 1);
        QCOMPARE(stopSpy.takeFirst().at(0).toULongLong(), stopId);

        client.disconnectFromServer();
        server.close();
    }

    void clientRequestsCaptureStartStop() {
        CanMonitorCore::CoreMaterializedViewStore store;
        const QString serverName = QStringLiteral("vsm-core-ipc-capture-test-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(reinterpret_cast<quintptr>(this));
        CanMonitorCore::CoreIpcServerRuntime server(&store);
        QString error;
        QVERIFY2(server.listen(serverName, &error), qPrintable(error));

        CanMonitorCore::CoreIpcClientRuntime client;
        QSignalSpy startSpy(&server, &CanMonitorCore::CoreIpcServerRuntime::captureStartRequested);
        QSignalSpy stopSpy(&server, &CanMonitorCore::CoreIpcServerRuntime::captureStopRequested);
        QSignalSpy updateSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::captureStorageUpdate);

        client.connectToServer(serverName);
        QTRY_VERIFY(client.isConnected());

        const quint64 startId = client.startCapture(QStringLiteral("unit-session"), QJsonObject{{QStringLiteral("source"), QStringLiteral("unit")}});
        QTRY_COMPARE(startSpy.size(), 1);
        QCOMPARE(startSpy.takeFirst().at(0).toULongLong(), startId);

        server.publishCaptureStorageUpdate(startId, true, QString(), true, true, QStringLiteral("unit-session"), true, 12, 3);
        QTRY_COMPARE(updateSpy.size(), 1);
        auto args = updateSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), startId);
        QCOMPARE(args.at(1).toBool(), true);
        QCOMPARE(args.at(4).toBool(), true);
        QCOMPARE(args.at(5).toString(), QStringLiteral("unit-session"));

        const quint64 stopId = client.stopCapture(QStringLiteral("unit-session"), QJsonObject{{QStringLiteral("reason"), QStringLiteral("unit-stop")}});
        QTRY_COMPARE(stopSpy.size(), 1);
        QCOMPARE(stopSpy.takeFirst().at(0).toULongLong(), stopId);

        client.disconnectFromServer();
        server.close();
    }

    void clientRequestsControlCycleCommands() {
        CanMonitorCore::CoreMaterializedViewStore store;
        const QString serverName = QStringLiteral("vsm-core-ipc-control-test-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(reinterpret_cast<quintptr>(this));
        CanMonitorCore::CoreIpcServerRuntime server(&store);
        QString error;
        QVERIFY2(server.listen(serverName, &error), qPrintable(error));

        CanMonitorCore::CoreIpcClientRuntime client;
        QSignalSpy controlSpy(&server, &CanMonitorCore::CoreIpcServerRuntime::controlCycleRequested);

        client.connectToServer(serverName);
        QTRY_VERIFY(client.isConnected());

        const quint64 startId = client.startControlCycle(120, 1000, 3.5, 1, 2, 1, 20, 2);
        QTRY_COMPARE(controlSpy.size(), 1);
        auto args = controlSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), startId);
        QCOMPARE(args.at(1).toString(), QStringLiteral("start"));
        auto payload = args.at(2).toJsonObject();
        QCOMPARE(payload.value(QStringLiteral("signed_command")).toInt(), 120);
        QCOMPARE(payload.value(QStringLiteral("rpm")).toInt(), 1000);
        QCOMPARE(payload.value(QStringLiteral("bus")).toInt(), 1);

        const quint64 updateId = client.updateControlCycle(0, 0, 0.0, 1, 1, 1);
        QTRY_COMPARE(controlSpy.size(), 1);
        args = controlSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), updateId);
        QCOMPARE(args.at(1).toString(), QStringLiteral("update"));

        const quint64 burstId = client.sendControlCycleBurstOnce(50, 500, -2.0, 1, 2, 1, QStringLiteral("unit burst"), true);
        QTRY_COMPARE(controlSpy.size(), 1);
        args = controlSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), burstId);
        QCOMPARE(args.at(1).toString(), QStringLiteral("burst_once"));
        payload = args.at(2).toJsonObject();
        QCOMPARE(payload.value(QStringLiteral("reason")).toString(), QStringLiteral("unit burst"));
        QCOMPARE(payload.value(QStringLiteral("reset_slew")).toBool(), true);

        const quint64 stopId = client.stopControlCycle();
        QTRY_COMPARE(controlSpy.size(), 1);
        args = controlSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), stopId);
        QCOMPARE(args.at(1).toString(), QStringLiteral("stop"));

        client.disconnectFromServer();
        server.close();
    }
};

QTEST_MAIN(CoreIpcRuntimeTest)
#include "test_core_ipc_runtime.moc"
