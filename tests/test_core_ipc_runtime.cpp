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
};

QTEST_MAIN(CoreIpcRuntimeTest)
#include "test_core_ipc_runtime.moc"
