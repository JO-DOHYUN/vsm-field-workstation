#include "core/CoreMaterializedViewStore.h"

#include <QJsonArray>
#include <QTest>

using namespace CanMonitorCore;

class CoreMaterializedViewStoreTest : public QObject {
    Q_OBJECT

private slots:
    void viewNameRoundTrips() {
        CoreViewName parsed = CoreViewName::CoreHealth;
        QVERIFY(coreViewNameFromString(QStringLiteral("live_latest"), &parsed));
        QCOMPARE(parsed, CoreViewName::LiveLatest);
        QCOMPARE(coreViewNameToString(parsed), QStringLiteral("live_latest"));

        CoreViewSeverity severity = CoreViewSeverity::Ok;
        QVERIFY(coreViewSeverityFromString(QStringLiteral("fatal"), &severity));
        QCOMPARE(severity, CoreViewSeverity::Fatal);
        QCOMPARE(coreViewSeverityToString(severity), QStringLiteral("fatal"));
    }

    void updateAndQueryUsesViewSeq() {
        CoreMaterializedViewStore store;

        QJsonObject payload;
        payload.insert(QStringLiteral("alive"), true);
        const auto changed = store.updateView(CoreViewName::CoreHealth, payload, CoreViewSeverity::Ok, {}, {}, 0, 123);
        QCOMPARE(changed.viewSeq, quint64(1));
        QCOMPARE(store.viewSeq(CoreViewName::CoreHealth), quint64(1));

        const auto firstQuery = store.queryView({CoreViewName::CoreHealth, 0, 0});
        QVERIFY(firstQuery.changed);
        QCOMPARE(firstQuery.snapshot.viewSeq, quint64(1));
        QCOMPARE(firstQuery.snapshot.updatedAtMs, qint64(123));
        QCOMPARE(firstQuery.snapshot.payload.value(QStringLiteral("alive")).toBool(), true);

        const auto noChangeQuery = store.queryView({CoreViewName::CoreHealth, 1, 0});
        QVERIFY(!noChangeQuery.changed);
    }

    void arrayViewIsBoundedAndCountsDroppedDisplayRows() {
        CoreMaterializedViewStore store;
        store.setPolicy(CoreViewName::RawLedgerTail, 3);

        QJsonArray rows;
        for (int i = 0; i < 5; ++i) {
            QJsonObject row;
            row.insert(QStringLiteral("n"), i);
            rows.append(row);
        }

        const auto changed = store.updateArrayView(CoreViewName::RawLedgerTail,
                                                   QStringLiteral("rows"),
                                                   rows,
                                                   CoreViewSeverity::Warn,
                                                   {},
                                                   {true, 10, 14},
                                                   456);
        QCOMPARE(changed.viewSeq, quint64(1));
        QCOMPARE(changed.severity, CoreViewSeverity::Warn);

        const auto query = store.queryView({CoreViewName::RawLedgerTail, 0, 0});
        QVERIFY(query.changed);
        const QJsonArray boundedRows = query.snapshot.payload.value(QStringLiteral("rows")).toArray();
        QCOMPARE(boundedRows.size(), 3);
        QCOMPARE(boundedRows.at(0).toObject().value(QStringLiteral("n")).toInt(), 2);
        QCOMPARE(boundedRows.at(2).toObject().value(QStringLiteral("n")).toInt(), 4);
        QCOMPARE(query.snapshot.droppedDisplayCount, quint64(2));
        QVERIFY(query.snapshot.sourceCaptureSeqRange.valid);
        QCOMPARE(query.snapshot.sourceCaptureSeqRange.first, quint64(10));
        QCOMPARE(query.snapshot.sourceCaptureSeqRange.last, quint64(14));
    }

    void queryLimitReturnsTailWithoutChangingStoredView() {
        CoreMaterializedViewStore store;
        store.setPolicy(CoreViewName::LiveLatest, 10);

        QJsonArray rows;
        for (int i = 0; i < 6; ++i) {
            rows.append(i);
        }
        store.updateArrayView(CoreViewName::LiveLatest, QStringLiteral("frames"), rows);

        const auto limited = store.queryView({CoreViewName::LiveLatest, 0, 2});
        QVERIFY(limited.changed);
        const QJsonArray limitedRows = limited.snapshot.payload.value(QStringLiteral("frames")).toArray();
        QCOMPARE(limitedRows.size(), 2);
        QCOMPARE(limitedRows.at(0).toInt(), 4);
        QCOMPARE(limitedRows.at(1).toInt(), 5);

        const auto full = store.queryView({CoreViewName::LiveLatest, 0, 0});
        QCOMPARE(full.snapshot.payload.value(QStringLiteral("frames")).toArray().size(), 6);
    }

    void arrayViewAcceptsExternalDropDelta() {
        CoreMaterializedViewStore store;
        store.setPolicy(CoreViewName::LiveLatest, 4);

        QJsonArray rows;
        rows.append(1);
        rows.append(2);

        store.updateArrayView(CoreViewName::LiveLatest,
                              QStringLiteral("frames"),
                              rows,
                              CoreViewSeverity::Ok,
                              {},
                              {},
                              -1,
                              3);

        const auto query = store.queryView({CoreViewName::LiveLatest, 0, 0});
        QVERIFY(query.changed);
        QCOMPARE(query.snapshot.droppedDisplayCount, quint64(3));
        QCOMPARE(query.change.cheapCounts.value(QStringLiteral("dropped_display_count")).toString(), QStringLiteral("3"));
    }
};

QTEST_MAIN(CoreMaterializedViewStoreTest)
#include "test_core_materialized_view_store.moc"
