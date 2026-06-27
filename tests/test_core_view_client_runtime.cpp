#include "core/CoreViewClientRuntime.h"

#include <QtTest/QtTest>

class CoreViewClientRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void singleFlightCoalescesViewChanges() {
        CanMonitorCore::CoreViewClientRuntime client;

        const QJsonObject change1 = makeChange(QStringLiteral("live_latest"), 10);
        auto request1 = client.noteViewChanged(change1);
        QVERIFY(request1.has_value());
        QCOMPARE(request1->viewName, QStringLiteral("live_latest"));
        QCOMPARE(request1->sinceSeq, quint64(0));
        QCOMPARE(request1->limit, 32);

        const QJsonObject change2 = makeChange(QStringLiteral("live_latest"), 11);
        auto request2 = client.noteViewChanged(change2);
        QVERIFY(!request2.has_value());
        QCOMPARE(client.status().skippedInflight, quint64(1));

        const auto apply1 = client.applySnapshot(request1->requestId, true, makeSnapshot(QStringLiteral("live_latest"), 10), change1);
        QVERIFY(apply1.accepted);
        QVERIFY(apply1.followup.has_value());
        QCOMPARE(apply1.followup->sinceSeq, quint64(10));
        QCOMPARE(apply1.followup->viewName, QStringLiteral("live_latest"));

        const auto apply2 = client.applySnapshot(apply1.followup->requestId, true, makeSnapshot(QStringLiteral("live_latest"), 11), change2);
        QVERIFY(apply2.accepted);
        QVERIFY(!apply2.followup.has_value());
        QCOMPARE(client.lastSeq(CanMonitorCore::CoreViewName::LiveLatest), quint64(11));

        const auto status = client.status();
        QCOMPARE(status.changedNotifications, quint64(2));
        QCOMPARE(status.queryRequests, quint64(2));
        QCOMPARE(status.queryResponses, quint64(2));
        QCOMPARE(status.pendingViews, 0);
        QCOMPARE(status.inflightViews, 0);
    }

    void disabledUnknownAndStaleAreAccounted() {
        CanMonitorCore::CoreViewClientRuntime client;
        client.setPolicy(CanMonitorCore::CoreViewName::RawLedgerTail, 32, false);

        QVERIFY(!client.noteViewChanged(makeChange(QStringLiteral("raw_ledger_tail"), 2)).has_value());
        QCOMPARE(client.status().skippedDisabled, quint64(1));

        QVERIFY(!client.noteViewChanged(QJsonObject{{QStringLiteral("view_name"), QStringLiteral("missing")}}).has_value());
        QCOMPARE(client.status().invalidChanges, quint64(1));

        auto request = client.noteViewChanged(makeChange(QStringLiteral("transport_summary"), 3));
        QVERIFY(request.has_value());
        const auto stale = client.applySnapshot(request->requestId + 99, true, makeSnapshot(QStringLiteral("transport_summary"), 3), makeChange(QStringLiteral("transport_summary"), 3));
        QVERIFY(stale.stale);
        QCOMPARE(client.status().staleResponses, quint64(1));
        QCOMPARE(client.status().inflightViews, 1);
    }

private:
    static QJsonObject makeChange(const QString& name, quint64 seq) {
        QJsonObject out;
        out.insert(QStringLiteral("view_name"), name);
        out.insert(QStringLiteral("view_seq"), QString::number(seq));
        out.insert(QStringLiteral("severity"), QStringLiteral("ok"));
        return out;
    }

    static QJsonObject makeSnapshot(const QString& name, quint64 seq) {
        QJsonObject out = makeChange(name, seq);
        out.insert(QStringLiteral("payload"), QJsonObject{});
        return out;
    }
};

QTEST_MAIN(CoreViewClientRuntimeTest)
#include "test_core_view_client_runtime.moc"
