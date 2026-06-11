#include "SignalDecoder.h"
#include "TimingEvaluator.h"
#include "FrameListModel.h"
#include "FrameFilterProxyModel.h"

#include <QtTest/QtTest>

namespace {

FrameRecord makeFrame(quint32 canId, quint8 dlc, std::initializer_list<quint8> bytes) {
    FrameRecord frame;
    frame.canId = canId;
    frame.dlc = dlc;
    int index = 0;
    for (quint8 value : bytes) {
        if (index >= 8) break;
        frame.data[index++] = value;
    }
    return frame;
}

CanModel::RuleSpec makeRule() {
    CanModel::RuleSpec rule;
    rule.id = 0x301;
    rule.name = QStringLiteral("Wheel Speed");
    rule.expectedPeriodMs = 100.0;
    rule.ttlWarnMs = 180.0;
    rule.ttlErrMs = 300.0;
    rule.periodWarnPct = 20.0;
    rule.periodErrPct = 50.0;
    rule.timingEnabled = true;
    return rule;
}

CanModel::SignalMessageSpec makeValueMessage() {
    CanModel::SignalMessageSpec msg;
    msg.id = 0x401;
    msg.name = QStringLiteral("Drive Status");

    CanModel::SignalSpec range;
    range.name = QStringLiteral("Motor Temperature");
    range.byteIndex1Based = 1;
    range.lengthBits = 8;
    range.alarmMode = QStringLiteral("range");
    range.unit = QStringLiteral("C");
    range.hasWarnMax = true;
    range.warnMax = 90.0;
    range.hasErrMax = true;
    range.errMax = 100.0;
    range.alarmSeverity = QStringLiteral("ERR");

    CanModel::SignalSpec reserved;
    reserved.name = QStringLiteral("Reserved Fault");
    reserved.byteIndex1Based = 2;
    reserved.lengthBits = 1;
    reserved.reserved = true;
    reserved.alarmMode = QStringLiteral("reserved");
    reserved.alarmMessage = QStringLiteral("Reserved bit set");

    CanModel::SignalSpec inactiveFlag;
    inactiveFlag.name = QStringLiteral("Run State");
    inactiveFlag.byteIndex1Based = 3;
    inactiveFlag.lengthBits = 2;
    inactiveFlag.alarmMode = QStringLiteral("flag");
    inactiveFlag.inactiveRawValues = {0};
    inactiveFlag.operatingText = QStringLiteral("0: Ready, 1: Running");

    msg.signalSpecs = {range, reserved, inactiveFlag};
    return msg;
}

} // namespace

class AnalysisSemanticsTest : public QObject {
    Q_OBJECT

private slots:
    void timingEvaluatorReturnsObservationWithoutRule() {
        CanMonitorAnalysis::TimingInput input;
        input.id = 0x301;
        input.displayName = QStringLiteral("Wheel Speed");
        input.source = QStringLiteral("replay");
        input.modelEnabled = false;
        input.seen = true;
        input.nowMs = 1000;
        input.lastLocalSeenMs = 900;
        input.gapMs = 100.0;

        const auto result = CanMonitorAnalysis::TimingEvaluator::evaluate(input);
        QCOMPARE(result.severity, QStringLiteral("관찰"));
        QCOMPARE(result.activeAlarm, false);
        QCOMPARE(result.gaugePct, 0.0);
        QCOMPARE(result.source, QStringLiteral("replay"));
    }

    void timingEvaluatorEscalatesWarnAndErr() {
        CanModel::RuleSpec rule = makeRule();

        CanMonitorAnalysis::TimingInput warnInput;
        warnInput.id = rule.id;
        warnInput.displayName = rule.name;
        warnInput.source = QStringLiteral("live");
        warnInput.modelEnabled = true;
        warnInput.seen = true;
        warnInput.nowMs = 250;
        warnInput.lastLocalSeenMs = 100;
        warnInput.gapMs = 125.0;
        warnInput.rule = &rule;

        const auto warn = CanMonitorAnalysis::TimingEvaluator::evaluate(warnInput);
        QCOMPARE(warn.severity, QStringLiteral("WARN"));
        QCOMPARE(warn.activeAlarm, true);
        QVERIFY(warn.alarmKey.contains(QStringLiteral("period_warn")));
        QVERIFY(warn.metricText.contains('%'));
        QCOMPARE(CanMonitorAnalysis::TimingEvaluator::timingAgeBucket(&rule, 200.0), 1);

        CanMonitorAnalysis::TimingInput errInput = warnInput;
        errInput.nowMs = 500;
        errInput.lastLocalSeenMs = 100;
        errInput.gapMs = 190.0;

        const auto err = CanMonitorAnalysis::TimingEvaluator::evaluate(errInput);
        QCOMPARE(err.severity, QStringLiteral("ERR"));
        QCOMPARE(err.activeAlarm, true);
        QVERIFY(err.alarmKey.contains(QStringLiteral("ttl_err")));
        QVERIFY(err.alarmKey.contains(QStringLiteral("period_err")));
        QCOMPARE(CanMonitorAnalysis::TimingEvaluator::timingAgeBucket(&rule, 350.0), 2);
    }

    void signalDecoderRaisesValueAlarmForThresholdAndReservedBit() {
        QHash<quint32, CanModel::SignalMessageSpec> messages;
        CanModel::SignalMessageSpec msg;
        msg.id = 0x401;
        msg.name = QStringLiteral("Drive Status");

        CanModel::SignalSpec range;
        range.name = QStringLiteral("Motor Temperature");
        range.byteIndex1Based = 1;
        range.lengthBits = 8;
        range.alarmMode = QStringLiteral("range");
        range.unit = QStringLiteral("C");
        range.hasWarnMax = true;
        range.warnMax = 90.0;
        range.hasErrMax = true;
        range.errMax = 100.0;
        range.alarmSeverity = QStringLiteral("ERR");

        CanModel::SignalSpec reserved;
        reserved.name = QStringLiteral("Reserved Fault");
        reserved.byteIndex1Based = 2;
        reserved.lengthBits = 1;
        reserved.reserved = true;
        reserved.alarmMode = QStringLiteral("reserved");
        reserved.alarmMessage = QStringLiteral("Reserved bit set");

        msg.signalSpecs = {range, reserved};
        messages.insert(0x401, msg);

        const FrameRecord frame = makeFrame(0x401, 2, {105, 0x01});
        const auto result = CanMonitorAnalysis::SignalDecoder::makeValueAlarm(0x401, frame, messages, true);

        QCOMPARE(result.active, true);
        QCOMPARE(result.severity, QStringLiteral("ERR"));
        QVERIFY(!result.message.trimmed().isEmpty());
        QCOMPARE(result.gaugePct, 100.0);
        QVERIFY(result.metricText.contains('%'));
        QVERIFY(result.alarmKey.startsWith(QStringLiteral("value|0X401|")));
    }

    void signalDecoderSuppressesInactiveFlagAndBuildsDetailRows() {
        QHash<quint32, CanModel::SignalMessageSpec> messages;
        messages.insert(0x401, makeValueMessage());

        const FrameRecord healthy = makeFrame(0x401, 3, {80, 0x00, 0x00});
        const auto healthyAlarm = CanMonitorAnalysis::SignalDecoder::makeValueAlarm(0x401, healthy, messages, true);
        QCOMPARE(healthyAlarm.active, false);

        const auto rows = CanMonitorAnalysis::SignalDecoder::makeDetailRows(0x401, healthy, messages, true);
        QVERIFY(rows.size() >= 4);
        QCOMPARE(rows.first().key, QStringLiteral("메시지"));
        QVERIFY(rows.first().value.contains(QStringLiteral("Drive Status")));

        bool foundThresholdNote = false;
        bool foundByteRow = false;
        for (const DetailRow& row : rows) {
            if (row.key.contains(QStringLiteral("Motor Temperature")) && row.note.contains(QStringLiteral("WARN["))) {
                foundThresholdNote = true;
            }
            if (row.key == QStringLiteral("BYTE 1") && row.note.contains(QStringLiteral("Motor Temperature"))) {
                foundByteRow = true;
            }
        }
        QVERIFY(foundThresholdNote);
        QVERIFY(foundByteRow);
    }

    void frameListModelExposesActualDlcAndCompactPayload() {
        FrameListModel model;
        FrameRecord frame = makeFrame(0x123, 3, {0xAA, 0xBB, 0xCC, 0xDD});
        frame.bus = 1;
        model.appendLive(frame);

        QCOMPARE(model.rowCount(), 1);
        const QModelIndex row = model.index(0, 0);
        QCOMPARE(model.data(row, FrameListModel::DlcRole).toInt(), 3);
        QCOMPARE(model.data(row, FrameListModel::BusRole).toInt(), 1);
        QCOMPARE(model.data(row, FrameListModel::DataHexRole).toString(), QStringLiteral("AA BB CC"));
    }

    void frameFilterProxyPreservesDlcRoleForReplayViews() {
        FrameListModel model;
        FrameRecord frame = makeFrame(0x123, 2, {0x11, 0x22, 0x33});
        model.appendReplay(frame);

        FrameFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        QCOMPARE(proxy.rowCount(), 1);
        QVERIFY(proxy.roleNames().values().contains(QByteArrayLiteral("dlc")));

        const QModelIndex row = proxy.index(0, 0);
        QCOMPARE(proxy.data(row, FrameListModel::DlcRole).toInt(), 2);
        QCOMPARE(proxy.data(row, FrameListModel::DataHexRole).toString(), QStringLiteral("11 22"));
    }

    void frameFilterProxyCombinesBusAndIdFilters() {
        FrameListModel model;
        FrameRecord bus0 = makeFrame(0x120, 8, {0x10, 0x20});
        bus0.bus = 0;
        FrameRecord bus1 = makeFrame(0x121, 8, {0x30, 0x40});
        bus1.bus = 1;
        FrameRecord bus1OtherId = makeFrame(0x220, 8, {0x50, 0x60});
        bus1OtherId.bus = 1;
        model.appendReplay(bus0);
        model.appendReplay(bus1);
        model.appendReplay(bus1OtherId);

        FrameFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        QCOMPARE(proxy.rowCount(), 3);

        proxy.setBusFilter(1);
        QCOMPARE(proxy.rowCount(), 2);
        QCOMPARE(proxy.data(proxy.index(0, 0), FrameListModel::BusRole).toInt(), 1);

        proxy.setIdFilter(QStringLiteral("0x121"));
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.data(proxy.index(0, 0), FrameListModel::IdTextRole).toString(), QStringLiteral("0X121"));

        proxy.setBusFilter(-1);
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setIdFilter(QStringLiteral("0x12, 0X220"));
        QCOMPARE(proxy.rowCount(), 3);
        proxy.setBusFilter(1);
        QCOMPARE(proxy.rowCount(), 2);
        proxy.setIdFilter(QString());
        QCOMPARE(proxy.rowCount(), 2);
        proxy.setBusFilter(-1);
        QCOMPARE(proxy.rowCount(), 3);
    }
};

QTEST_APPLESS_MAIN(AnalysisSemanticsTest)

#include "test_analysis_semantics.moc"
