#include "control/ControlAuditModel.h"

#include <QtTest/QtTest>

class ControlAuditModelTest : public QObject {
    Q_OBJECT

private slots:
    void startsWithSeparatedEvidenceStagesAndZeroCounters() {
        CanMonitorControl::ControlAuditModel audit;

        const QString stats = audit.statsSummary();
        QVERIFY(stats.contains(QStringLiteral("요청 0")));
        QVERIFY(stats.contains(QStringLiteral("Qt write OK/Fail 0/0")));
        QVERIFY(stats.contains(QStringLiteral("ACK OK/Reject 0/0")));
        QVERIFY(stats.contains(QStringLiteral("TX audit 대기/매칭/미매칭 0/0/0")));
        QVERIFY(stats.contains(QStringLiteral("feedback 0")));

        const QVariantList stages = audit.stages();
        QCOMPARE(stages.size(), 6);
        QCOMPARE(stages.at(2).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("ack"));
        QCOMPARE(stages.at(3).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("tx"));
        QVERIFY(stages.at(2).toMap().value(QStringLiteral("summary")).toString().contains(QStringLiteral("No CONTROL_ACK")));
        QVERIFY(stages.at(3).toMap().value(QStringLiteral("summary")).toString().contains(QStringLiteral("No CAN_TX_RAW")));
        QCOMPARE(stages.at(2).toMap().value(QStringLiteral("successAuthority")).toBool(), false);
        QCOMPARE(stages.at(2).toMap().value(QStringLiteral("authority")).toString(), QStringLiteral("board_acceptance_only"));
        QCOMPARE(stages.at(3).toMap().value(QStringLiteral("successAuthority")).toBool(), true);
        QCOMPARE(stages.at(3).toMap().value(QStringLiteral("authority")).toString(), QStringLiteral("actual_can_tx"));
        QCOMPARE(audit.actualTxConfirmed(), false);
        QCOMPARE(audit.faultActive(), false);
    }

    void tracksAckPendingAuditAndActualTxSeparately() {
        CanMonitorControl::ControlAuditModel audit;

        audit.noteHostFrameQueued();
        audit.noteHostWriteResult(true);
        audit.rememberAcceptedAck(0x503, 7);
        audit.noteAck(true);
        audit.appendEvent(QStringLiteral("CONTROL_ACK"),
                          QStringLiteral("info"),
                          QStringLiteral("accepted"),
                          QStringLiteral("ACK #7 ACCEPTED"),
                          7,
                          0x503,
                          1);

        QVERIFY(audit.statsSummary().contains(QStringLiteral("요청 1")));
        QVERIFY(audit.statsSummary().contains(QStringLiteral("ACK OK/Reject 1/0")));
        QVERIFY(audit.statsSummary().contains(QStringLiteral("TX audit 대기/매칭/미매칭 1/0/0")));
        QCOMPARE(audit.actualTxConfirmed(), false);
        QCOMPARE(audit.takeAcceptedCommandId(0x503), quint32(7));
        audit.noteTxAudit(true);
        audit.appendEvent(QStringLiteral("CAN_TX_RAW"),
                          QStringLiteral("ok"),
                          QStringLiteral("actual tx"),
                          QStringLiteral("AUDIT TX BUS 1 0X503 DLC 8"),
                          7,
                          0x503,
                          1);

        QVERIFY(audit.statsSummary().contains(QStringLiteral("TX audit 대기/매칭/미매칭 0/1/0")));
        QCOMPARE(audit.stages().at(3).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("ok"));
        QVERIFY(audit.stages().at(3).toMap().value(QStringLiteral("summary")).toString().contains(QStringLiteral("AUDIT TX")));
        QCOMPARE(audit.actualTxConfirmed(), true);
        QCOMPARE(audit.faultActive(), false);
    }

    void actualTxAuditClearsRecoveredFaultState() {
        CanMonitorControl::ControlAuditModel audit;
        audit.appendEvent(QStringLiteral("CONTROL_ACK"),
                          QStringLiteral("error"),
                          QStringLiteral("rejected"),
                          QStringLiteral("ACK rejected before arm"),
                          2,
                          0x503,
                          0);
        QCOMPARE(audit.faultActive(), true);

        audit.appendEvent(QStringLiteral("CAN_TX_RAW"),
                          QStringLiteral("ok"),
                          QStringLiteral("actual tx"),
                          QStringLiteral("AUDIT TX BUS 0 0X503 DLC 8"),
                          3,
                          0x503,
                          0);

        QCOMPARE(audit.actualTxConfirmed(), true);
        QCOMPARE(audit.faultActive(), false);
        QCOMPARE(audit.stages().at(5).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("ok"));
    }

    void resetClearsRowsAndCounters() {
        CanMonitorControl::ControlAuditModel audit;
        audit.noteHostFrameQueued();
        audit.noteHostWriteResult(false);
        audit.appendEvent(QStringLiteral("BLOCKED"),
                          QStringLiteral("error"),
                          QStringLiteral("blocked"),
                          QStringLiteral("Control request was not sent"));
        QVERIFY(audit.model()->rowCount() > 0);
        QCOMPARE(audit.faultActive(), true);

        audit.reset();
        QCOMPARE(audit.model()->rowCount(), 0);
        QVERIFY(audit.statsSummary().contains(QStringLiteral("요청 0")));
        QCOMPARE(audit.stages().at(5).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("ok"));
        QCOMPARE(audit.faultActive(), false);
    }
};

QTEST_APPLESS_MAIN(ControlAuditModelTest)

#include "test_control_audit_model.moc"
