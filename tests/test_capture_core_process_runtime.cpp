#include "core/CaptureCoreProcessRuntime.h"
#include "core/CoreIpcClientRuntime.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

class CaptureCoreProcessRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void servesInitialViewsOverIpc() {
        const QString serverName = QStringLiteral("vsm-core-process-test-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(reinterpret_cast<quintptr>(this));

        CanMonitorCore::CaptureCoreProcessRuntime runtime;
        QString error;
        QVERIFY2(runtime.startIpc(serverName, &error), qPrintable(error));
        QVERIFY(runtime.isIpcListening());

        CanMonitorCore::CoreIpcClientRuntime client;
        QSignalSpy connectedSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::connectedChanged);
        QSignalSpy snapshotSpy(&client, &CanMonitorCore::CoreIpcClientRuntime::viewSnapshotReceived);

        client.connectToServer(serverName);
        QTRY_VERIFY(client.isConnected());
        QVERIFY(!connectedSpy.isEmpty());

        const quint64 healthRequest = client.requestView(QStringLiteral("core_health"), 0, 1);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        auto args = snapshotSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), healthRequest);
        QCOMPARE(args.at(1).toBool(), true);
        QJsonObject snapshot = args.at(2).toJsonObject();
        QCOMPARE(snapshot.value(QStringLiteral("payload")).toObject().value(QStringLiteral("process")).toString(),
                 QStringLiteral("vsm-capture-core"));

        const quint64 transportRequest = client.requestView(QStringLiteral("transport_summary"), 0, 1);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        args = snapshotSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), transportRequest);
        QCOMPARE(args.at(1).toBool(), true);
        snapshot = args.at(2).toJsonObject();
        QCOMPARE(snapshot.value(QStringLiteral("payload")).toObject().value(QStringLiteral("serial_owner")).toString(),
                 QStringLiteral("core"));

        const quint64 rawLedgerRequest = client.requestView(QStringLiteral("raw_ledger_tail"), 0, 16);
        QTRY_COMPARE(snapshotSpy.size(), 1);
        args = snapshotSpy.takeFirst();
        QCOMPARE(args.at(0).toULongLong(), rawLedgerRequest);
        QCOMPARE(args.at(1).toBool(), true);
        snapshot = args.at(2).toJsonObject();
        const QJsonObject rawPayload = snapshot.value(QStringLiteral("payload")).toObject();
        QCOMPARE(rawPayload.value(QStringLiteral("source")).toString(),
                 QStringLiteral("core_raw_ledger_writer"));
        QCOMPARE(rawPayload.value(QStringLiteral("frames")).toArray().size(), 0);

        client.disconnectFromServer();
        runtime.stopIpc();
    }
};

QTEST_MAIN(CaptureCoreProcessRuntimeTest)
#include "test_capture_core_process_runtime.moc"
