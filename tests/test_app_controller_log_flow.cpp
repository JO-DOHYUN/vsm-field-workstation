#include "AppController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

namespace {

QString writeModelFixture(const QString& rootPath) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("can-monitor-model-pack.v1"));
    root.insert(QStringLiteral("model_key"), QStringLiteral("app_controller_log_fixture"));
    root.insert(QStringLiteral("model_name"), QStringLiteral("Log Flow Fixture"));
    root.insert(QStringLiteral("model_version"), QStringLiteral("2026-04-17"));
    root.insert(QStringLiteral("vendor"), QStringLiteral("Codex"));

    QJsonObject rule;
    rule.insert(QStringLiteral("id"), QStringLiteral("0x321"));
    rule.insert(QStringLiteral("name_en"), QStringLiteral("LOG_FLOW_FIXTURE"));
    rule.insert(QStringLiteral("expected_period_ms"), 100.0);

    QJsonObject signal;
    signal.insert(QStringLiteral("name"), QStringLiteral("Motor Temperature"));
    signal.insert(QStringLiteral("byte_index_1based"), 1);
    signal.insert(QStringLiteral("bit_text"), QStringLiteral("8..1"));
    signal.insert(QStringLiteral("length_bits"), 8);
    signal.insert(QStringLiteral("start_bit_lsb"), 0);
    signal.insert(QStringLiteral("bit_positions_lsb"), QJsonArray{});
    signal.insert(QStringLiteral("scale"), 1.0);
    signal.insert(QStringLiteral("offset"), 0.0);
    signal.insert(QStringLiteral("signed"), false);
    signal.insert(QStringLiteral("range_text"), QStringLiteral("0 to 100"));
    signal.insert(QStringLiteral("operating_text"), QStringLiteral("unit: 1 C"));
    signal.insert(QStringLiteral("description"), QStringLiteral("fixture temperature"));
    signal.insert(QStringLiteral("reserved"), false);
    signal.insert(QStringLiteral("unit"), QStringLiteral("C"));

    QJsonObject message;
    message.insert(QStringLiteral("id"), QStringLiteral("0x321"));
    message.insert(QStringLiteral("name"), QStringLiteral("Log Flow Frame"));
    message.insert(QStringLiteral("signals"), QJsonArray{signal});

    QJsonObject policyRule;
    policyRule.insert(QStringLiteral("role"), QStringLiteral("system"));
    policyRule.insert(QStringLiteral("tx_allowed"), true);
    policyRule.insert(QStringLiteral("fingerprints"), QJsonArray{QStringLiteral("0x321")});

    QJsonObject policy;
    policy.insert(QStringLiteral("enabled"), true);
    policy.insert(QStringLiteral("profile_name"), QStringLiteral("log-flow-policy"));
    policy.insert(QStringLiteral("target_role"), QStringLiteral("system"));
    policy.insert(QStringLiteral("allowed_bus_roles"), QJsonArray{QStringLiteral("system")});
    policy.insert(QStringLiteral("max_rpm"), 1234);
    policy.insert(QStringLiteral("max_abs_steering_deg"), 12.5);
    policy.insert(QStringLiteral("bus_role_rules"), QJsonArray{policyRule});

    root.insert(QStringLiteral("control_policy"), policy);
    root.insert(QStringLiteral("rules"), QJsonArray{rule});
    root.insert(QStringLiteral("messages"), QJsonArray{message});

    const QString path = rootPath + QStringLiteral("/log_flow_model.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return path;
}

} // namespace

class AppControllerLogFlowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void controlEvidenceStatsStartAtZero() {
        AppController controller;
        const QString stats = controller.controlEvidenceStatsSummary();

        QVERIFY(stats.contains(QStringLiteral("요청 0")));
        QVERIFY(stats.contains(QStringLiteral("Qt write OK/Fail 0/0")));
        QVERIFY(stats.contains(QStringLiteral("ACK OK/Reject 0/0")));
        QVERIFY(stats.contains(QStringLiteral("TX audit 대기/매칭/미매칭 0/0/0")));
    }

    void defaultStorageDirectoriesUseProjectReplayData() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QByteArray appDataPath = QDir::toNativeSeparators(tempDir.path()).toUtf8();
        qputenv("APPDATA", appDataPath);
        qputenv("LOCALAPPDATA", appDataPath);

        AppController controller;
        const QString logDir = QDir::fromNativeSeparators(controller.defaultLogDirectory());
        const QString snapshotDir = QDir::fromNativeSeparators(controller.defaultSnapshotDirectory());

        QVERIFY(logDir.contains(QStringLiteral("/replay_data/logs")));
        QVERIFY(snapshotDir.contains(QStringLiteral("/replay_data/snapshots")));
        QVERIFY(!logDir.startsWith(QDir::fromNativeSeparators(tempDir.path())));
        QVERIFY(QFileInfo::exists(logDir));
        QVERIFY(QFileInfo::exists(snapshotDir));
    }

    void logTargetPreviewUsesOperatorDirectoryAndName() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        AppController controller;
        const QString targetDir = tempDir.path() + QStringLiteral("/field logs");
        controller.setLogTargetDirectory(QUrl::fromLocalFile(targetDir).toString());
        controller.setLogTargetName(QStringLiteral(" tomorrow bus0/bus1 <> "));

        QCOMPARE(QDir::cleanPath(controller.logTargetDirectory()), QDir::cleanPath(targetDir));
        QCOMPARE(controller.logTargetName(), QStringLiteral("tomorrow bus0/bus1 <>"));
        QVERIFY(controller.logTargetPreview().contains(QStringLiteral("tomorrow_bus0_bus1")));
        QVERIFY(controller.logTargetPreview().endsWith(QStringLiteral(".typed")));
        QVERIFY(controller.suggestedSnapshotPath().contains(QStringLiteral("/replay_data/snapshots")));
    }

    void modelPackControlPolicySurfacesAndClampsTargets() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString modelPath = writeModelFixture(tempDir.path());
        QVERIFY(!modelPath.isEmpty());

        AppController controller;
        controller.clearSavedSession();
        controller.setRulesPath(modelPath);
        QTRY_VERIFY_WITH_TIMEOUT(controller.modelActive(), 10000);

        QVERIFY(controller.controlPolicySummary().contains(QStringLiteral("log-flow-policy")));
        QVERIFY(controller.controlPolicySummary().contains(QStringLiteral("limit rpm 1234")));
        controller.setControlTargetRpm(5000);
        QCOMPARE(controller.controlTargetRpm(), 1234);
        controller.setControlTargetSteeringDeg(30.0);
        QCOMPARE(controller.controlTargetSteeringDeg(), 12.5);
    }

    void keyboardDriveRequiresHeldKeysAndCentersOnRelease() {
        AppController controller;
        controller.setControlTargetRpm(1000);

        controller.controlKeyboardPress(QStringLiteral("w"));
        auto intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 1000);
        QCOMPARE(intent.rpm, 1000);
        QCOMPARE(intent.steeringDeg, 0.0);
        QCOMPARE(intent.drivingMode, quint8(2));
        QVERIFY(controller.m_controlKeyboardSessionActive);
        QVERIFY(controller.m_controlKeyboardHeldKeys.contains(QStringLiteral("w")));

        controller.controlKeyboardRelease(QStringLiteral("w"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 0);
        QCOMPARE(intent.rpm, 0);
        QCOMPARE(intent.steeringDeg, 0.0);
        QCOMPARE(intent.drivingMode, quint8(1));
        QVERIFY(!controller.m_controlKeyboardSessionActive);
        QVERIFY(controller.m_controlKeyboardHeldKeys.isEmpty());

        controller.controlKeyboardPress(QStringLiteral("a"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 0);
        QCOMPARE(intent.rpm, 0);
        QVERIFY(intent.steeringDeg < 0.0);
        QCOMPARE(intent.steeringDeg, -45.0);
        QCOMPARE(controller.controlTargetSteeringDeg(), intent.steeringDeg);

        controller.controlKeyboardRelease(QStringLiteral("a"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 0);
        QCOMPARE(intent.rpm, 0);
        QCOMPARE(intent.steeringDeg, 0.0);
        QCOMPARE(controller.controlTargetSteeringDeg(), 0.0);

        controller.controlKeyboardPress(QStringLiteral("w"));
        controller.controlKeyboardPress(QStringLiteral("d"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 1000);
        QCOMPARE(intent.rpm, 1000);
        QVERIFY(intent.steeringDeg > 0.0);
        QVERIFY(controller.m_controlKeyboardHeldKeys.contains(QStringLiteral("w")));
        QVERIFY(controller.m_controlKeyboardHeldKeys.contains(QStringLiteral("d")));

        controller.controlKeyboardRelease(QStringLiteral("d"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 1000);
        QCOMPARE(intent.rpm, 1000);
        QCOMPARE(intent.steeringDeg, 0.0);
        QVERIFY(controller.m_controlKeyboardHeldKeys.contains(QStringLiteral("w")));

        controller.controlKeyboardReleaseAll();
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 0);
        QCOMPARE(intent.rpm, 0);
        QCOMPARE(intent.steeringDeg, 0.0);
        QVERIFY(controller.m_controlKeyboardHeldKeys.isEmpty());

        controller.controlKeyboardPress(QStringLiteral("w"));
        controller.controlKeyboardPress(QStringLiteral("s"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 0);
        QCOMPARE(intent.rpm, 0);
        QVERIFY(controller.m_controlKeyboardSessionActive);

        controller.controlKeyboardRelease(QStringLiteral("s"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 1000);
        QCOMPARE(intent.rpm, 1000);

        controller.controlKeyboardPress(QStringLiteral("space"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 0);
        QCOMPARE(intent.rpm, 0);
        QCOMPARE(intent.steeringDeg, 0.0);
        QCOMPARE(intent.drivingMode, quint8(1));
        QVERIFY(controller.m_controlKeyboardHeldKeys.isEmpty());

        controller.controlKeyboardPress(QStringLiteral("2"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 0);
        QCOMPARE(intent.rpm, 0);
        QCOMPARE(intent.steeringDeg, 90.0);
        QCOMPARE(intent.drivingMode, quint8(6));

        controller.controlKeyboardPress(QStringLiteral("3"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 0);
        QCOMPARE(intent.rpm, 0);
        QCOMPARE(intent.steeringDeg, -90.0);
        QCOMPARE(intent.drivingMode, quint8(6));

        controller.controlKeyboardPress(QStringLiteral("1"));
        intent = controller.m_controlRuntime.currentIntent();
        QCOMPARE(intent.signedCommand, 0);
        QCOMPARE(intent.rpm, 0);
        QCOMPARE(intent.steeringDeg, 0.0);
        QCOMPARE(intent.drivingMode, quint8(1));
    }

    void controlEvidenceStagesSeparateAckFromActualTx() {
        AppController controller;

        QCOMPARE(controller.controlReady(), false);
        QVERIFY(controller.controlBlockReason().contains(QStringLiteral("COM not connected")));
        QVERIFY(controller.controlOperatorSummary().contains(QStringLiteral("제어 차단")));
        QVERIFY(controller.controlOperatorSummary().contains(QStringLiteral("CAN_TX_RAW 미확인")));
        QVERIFY(controller.transportDiagnosticsSummary().contains(QStringLiteral("transport")));
        QCOMPARE(controller.transportDiagnostics().size(), 6);
        QVERIFY(controller.controlActionVerdict().contains(QStringLiteral("COM 연결 없음")));
        QCOMPARE(controller.controlOperatorChecklist().size(), 8);
        QCOMPARE(controller.controlPolicyChecklist().size(), 1);
        QVERIFY(controller.controlPolicySummary().contains(QStringLiteral("default policy")));

        auto stageMap = [&controller](const QString& key) {
            const QVariantList stages = controller.controlEvidenceStages();
            for (const QVariant& value : stages) {
                const QVariantMap row = value.toMap();
                if (row.value(QStringLiteral("key")).toString() == key) return row;
            }
            return QVariantMap{};
        };

        QCOMPARE(controller.controlEvidenceStages().size(), 6);
        auto checklistMap = [&controller](const QString& key) {
            const QVariantList rows = controller.controlOperatorChecklist();
            for (const QVariant& value : rows) {
                const QVariantMap row = value.toMap();
                if (row.value(QStringLiteral("key")).toString() == key) return row;
            }
            return QVariantMap{};
        };
        QCOMPARE(checklistMap(QStringLiteral("serial")).value(QStringLiteral("level")).toString(), QStringLiteral("error"));
        QCOMPARE(checklistMap(QStringLiteral("policy")).value(QStringLiteral("state")).toString(), QStringLiteral("BLOCK"));
        QCOMPARE(checklistMap(QStringLiteral("tx")).value(QStringLiteral("state")).toString(), QStringLiteral("NO AUDIT"));
        QCOMPARE(checklistMap(QStringLiteral("tx")).value(QStringLiteral("blocking")).toBool(), false);
        QVERIFY(stageMap(QStringLiteral("ack")).value(QStringLiteral("summary")).toString().contains(QStringLiteral("No CONTROL_ACK")));
        QVERIFY(stageMap(QStringLiteral("tx")).value(QStringLiteral("summary")).toString().contains(QStringLiteral("No CAN_TX_RAW")));
        QCOMPARE(stageMap(QStringLiteral("ack")).value(QStringLiteral("authority")).toString(), QStringLiteral("board_acceptance_only"));
        QCOMPARE(stageMap(QStringLiteral("ack")).value(QStringLiteral("successAuthority")).toBool(), false);
        QCOMPARE(stageMap(QStringLiteral("tx")).value(QStringLiteral("authority")).toString(), QStringLiteral("actual_can_tx"));
        QCOMPARE(stageMap(QStringLiteral("tx")).value(QStringLiteral("successAuthority")).toBool(), true);

        controller.appendControlEvidenceEvent(QStringLiteral("REQUEST"),
                                              QStringLiteral("info"),
                                              QStringLiteral("manual command"),
                                              QStringLiteral("#7 0x503 BUS 0"),
                                              7,
                                              0x503,
                                              0);
        controller.appendControlEvidenceEvent(QStringLiteral("CONTROL_ACK"),
                                              QStringLiteral("info"),
                                              QStringLiteral("보드 요청 수락"),
                                              QStringLiteral("ACK #7 ACCEPTED; waiting for CAN_TX_RAW actual sent audit"),
                                              7,
                                              0x503,
                                              0);

        QVERIFY(stageMap(QStringLiteral("request")).value(QStringLiteral("summary")).toString().contains(QStringLiteral("#7")));
        QVERIFY(stageMap(QStringLiteral("ack")).value(QStringLiteral("summary")).toString().contains(QStringLiteral("ACCEPTED")));
        QVERIFY(stageMap(QStringLiteral("tx")).value(QStringLiteral("summary")).toString().contains(QStringLiteral("No CAN_TX_RAW")));
        QCOMPARE(stageMap(QStringLiteral("ack")).value(QStringLiteral("level")).toString(), QStringLiteral("info"));
        QCOMPARE(controller.controlActualTxConfirmed(), false);

        controller.appendControlEvidenceEvent(QStringLiteral("CAN_TX_RAW"),
                                              QStringLiteral("ok"),
                                              QStringLiteral("실제 CAN 송신 audit 확인"),
                                              QStringLiteral("AUDIT TX BUS 0 0x503 DLC 8"),
                                              7,
                                              0x503,
                                              0);
        QVERIFY(stageMap(QStringLiteral("tx")).value(QStringLiteral("summary")).toString().contains(QStringLiteral("AUDIT TX")));
        QCOMPARE(stageMap(QStringLiteral("tx")).value(QStringLiteral("level")).toString(), QStringLiteral("ok"));
        QCOMPARE(controller.controlActualTxConfirmed(), true);
        QVERIFY(controller.controlOperatorSummary().contains(QStringLiteral("CAN_TX_RAW 확인")));
        QCOMPARE(checklistMap(QStringLiteral("tx")).value(QStringLiteral("state")).toString(), QStringLiteral("CAN_TX_RAW"));

        controller.appendControlEvidenceEvent(QStringLiteral("BLOCKED"),
                                              QStringLiteral("error"),
                                              QStringLiteral("arm required before motion command"),
                                              QStringLiteral("Control request was not sent"));
        QCOMPARE(stageMap(QStringLiteral("fault")).value(QStringLiteral("level")).toString(), QStringLiteral("error"));
        QVERIFY(stageMap(QStringLiteral("fault")).value(QStringLiteral("summary")).toString().contains(QStringLiteral("not sent")));
        QCOMPARE(controller.controlFaultActive(), true);
        QVERIFY(controller.controlLastFaultSummary().contains(QStringLiteral("not sent")));
        QCOMPARE(checklistMap(QStringLiteral("fault")).value(QStringLiteral("level")).toString(), QStringLiteral("error"));
        QCOMPARE(checklistMap(QStringLiteral("fault")).value(QStringLiteral("blocking")).toBool(), true);
    }

    void finalizePendingLogSaveCopiesArtifactsAndClearsPendingState() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QByteArray appDataPath = QDir::toNativeSeparators(tempDir.path()).toUtf8();
        qputenv("APPDATA", appDataPath);
        qputenv("LOCALAPPDATA", appDataPath);

        const QString modelPath = writeModelFixture(tempDir.path());
        QVERIFY(!modelPath.isEmpty());

        AppController controller;
        controller.clearSavedSession();
        controller.clearFrames();
        controller.setTransportMode(QStringLiteral("legacy20"));
        controller.setRulesPath(modelPath);
        QTRY_VERIFY_WITH_TIMEOUT(controller.modelActive(), 10000);

        controller.m_connected = true;
        controller.startLog();
        QTRY_VERIFY_WITH_TIMEOUT(controller.logRecordingActive(), 10000);
        QVERIFY(!controller.logPath().isEmpty());
        QVERIFY(QFileInfo::exists(controller.logPath()));
        QCOMPARE(controller.logPendingSave(), false);

        controller.stopLog();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.logRecordingActive(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.logPendingSave(), 10000);
        QVERIFY(controller.logStatusSummary().contains(QStringLiteral("저장"), Qt::CaseInsensitive));

        const QString saveBasePath = tempDir.path() + QStringLiteral("/saved/capture_one");
        controller.finalizePendingLogSave(saveBasePath);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.logPendingSave(), 10000);
        QCOMPARE(controller.logSaving(), false);

        const QString finalBin = tempDir.path() + QStringLiteral("/saved/capture_one.bin");
        const QString finalMeta = tempDir.path() + QStringLiteral("/saved/capture_one.meta.json");
        const QString finalModel = tempDir.path() + QStringLiteral("/saved/capture_one.model.json");
        QCOMPARE(controller.logPath(), finalBin);
        QCOMPARE(controller.suggestedLogSavePath(), finalBin);
        QCOMPARE(controller.m_logLastSavedPath, finalBin);
        QVERIFY(QFileInfo::exists(finalBin));
        QVERIFY(QFileInfo::exists(finalMeta));
        QVERIFY(QFileInfo::exists(finalModel));
    }

    void discardPendingLogRemovesTempArtifactsAndClearsPendingState() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QByteArray appDataPath = QDir::toNativeSeparators(tempDir.path()).toUtf8();
        qputenv("APPDATA", appDataPath);
        qputenv("LOCALAPPDATA", appDataPath);

        const QString modelPath = writeModelFixture(tempDir.path());
        QVERIFY(!modelPath.isEmpty());

        AppController controller;
        controller.clearSavedSession();
        controller.clearFrames();
        controller.setTransportMode(QStringLiteral("legacy20"));
        controller.setRulesPath(modelPath);
        QTRY_VERIFY_WITH_TIMEOUT(controller.modelActive(), 10000);

        controller.m_connected = true;
        controller.startLog();
        QTRY_VERIFY_WITH_TIMEOUT(controller.logRecordingActive(), 10000);
        const QString tempBin = controller.m_logTempPath;
        const QString tempMeta = controller.m_logTempMetaPath;
        const QString tempModel = controller.m_logTempModelPath;
        QVERIFY(!tempBin.isEmpty());

        controller.stopLog();
        QTRY_VERIFY_WITH_TIMEOUT(controller.logPendingSave(), 10000);
        controller.discardPendingLog();

        QCOMPARE(controller.logPendingSave(), false);
        QCOMPARE(controller.logRecordingActive(), false);
        QCOMPARE(controller.logStopping(), false);
        QCOMPARE(controller.logSaving(), false);
        QVERIFY(!QFileInfo::exists(tempBin));
        QVERIFY(!QFileInfo::exists(tempMeta));
        QVERIFY(!QFileInfo::exists(tempModel));
        QVERIFY(controller.logPath().isEmpty());
    }
};

QTEST_GUILESS_MAIN(AppControllerLogFlowTest)

#include "test_app_controller_log_flow.moc"
