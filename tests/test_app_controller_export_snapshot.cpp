#include "AppController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

quint8 crc8Atm(const QByteArray& bytes) {
    quint8 crc = 0x00;
    for (const char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) crc = quint8((crc << 1) ^ 0x07);
            else crc = quint8(crc << 1);
        }
    }
    return crc;
}

QByteArray framePacket(quint32 tus, quint32 canId, quint8 bus, quint8 seq, const QByteArray& payload) {
    QByteArray packet(20, Qt::Uninitialized);
    packet[0] = char(tus & 0xFF);
    packet[1] = char((tus >> 8) & 0xFF);
    packet[2] = char((tus >> 16) & 0xFF);
    packet[3] = char((tus >> 24) & 0xFF);
    packet[4] = char(canId & 0xFF);
    packet[5] = char((canId >> 8) & 0xFF);
    packet[6] = char((canId >> 16) & 0xFF);
    packet[7] = char((canId >> 24) & 0x1F);
    packet[8] = char(payload.size() & 0x0F);
    for (int index = 0; index < 8; ++index) {
        packet[9 + index] = index < payload.size() ? payload.at(index) : char(0);
    }
    packet[17] = char(bus & 0x03);
    packet[18] = char(seq);
    packet[19] = char(crc8Atm(packet.left(19)));
    return packet;
}

QString writeModelFixture(const QString& rootPath) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("can-monitor-model-pack.v1"));
    root.insert(QStringLiteral("model_key"), QStringLiteral("export_snapshot_fixture"));
    root.insert(QStringLiteral("model_name"), QStringLiteral("Export Snapshot Fixture"));
    root.insert(QStringLiteral("model_version"), QStringLiteral("2026-04-17"));
    root.insert(QStringLiteral("vendor"), QStringLiteral("Codex"));

    QJsonObject rule;
    rule.insert(QStringLiteral("id"), QStringLiteral("0x321"));
    rule.insert(QStringLiteral("name_en"), QStringLiteral("EXPORT_SNAPSHOT_FIXTURE"));
    rule.insert(QStringLiteral("expected_period_ms"), 100.0);
    rule.insert(QStringLiteral("ttl_warn_ms"), 500.0);
    rule.insert(QStringLiteral("ttl_err_ms"), 1000.0);
    rule.insert(QStringLiteral("period_err_warn_pct"), 20.0);
    rule.insert(QStringLiteral("period_err_err_pct"), 50.0);

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
    signal.insert(QStringLiteral("alarm_mode"), QStringLiteral("range"));
    signal.insert(QStringLiteral("warn_max"), 50.0);
    signal.insert(QStringLiteral("err_max"), 60.0);
    signal.insert(QStringLiteral("alarm_severity"), QStringLiteral("ERR"));
    signal.insert(QStringLiteral("alarm_message"), QStringLiteral("Temperature too high"));

    QJsonObject message;
    message.insert(QStringLiteral("id"), QStringLiteral("0x321"));
    message.insert(QStringLiteral("name"), QStringLiteral("Export Snapshot Frame"));
    message.insert(QStringLiteral("signals"), QJsonArray{signal});

    root.insert(QStringLiteral("rules"), QJsonArray{rule});
    root.insert(QStringLiteral("messages"), QJsonArray{message});

    const QString path = rootPath + QStringLiteral("/export_snapshot_model.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return path;
}

QString writeReplayFixture(const QString& rootPath) {
    const QString path = rootPath + QStringLiteral("/export_snapshot_replay.bin");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(framePacket(100000, 0x321, 0, 1, QByteArray::fromHex("2800000000000000")));
    file.write(framePacket(260000, 0x321, 0, 2, QByteArray::fromHex("4600000000000000")));
    file.close();
    return path;
}

QString readUtf8File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

class AppControllerExportSnapshotTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void exportSnapshotCarriesCurrentOperatorSummaryIntoJsonAndMarkdown() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QByteArray appDataPath = QDir::toNativeSeparators(tempDir.path()).toUtf8();
        qputenv("APPDATA", appDataPath);
        qputenv("LOCALAPPDATA", appDataPath);

        const QString modelPath = writeModelFixture(tempDir.path());
        const QString replayPath = writeReplayFixture(tempDir.path());
        QVERIFY(!modelPath.isEmpty());
        QVERIFY(!replayPath.isEmpty());

        AppController controller;
        controller.clearSavedSession();
        controller.clearFrames();
        controller.setRulesPath(modelPath);
        QTRY_VERIFY_WITH_TIMEOUT(controller.modelActive(), 10000);

        controller.loadReplay(replayPath);
        QTRY_VERIFY_WITH_TIMEOUT(controller.replayLoaded(), 10000);
        QVERIFY(controller.jumpReplayToFrameIndex(1, QStringLiteral("snapshot baseline")));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.replayRebuilding(), 10000);

        controller.pauseReplay();
        QTRY_VERIFY_WITH_TIMEOUT(controller.replayAnalysisHeld(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.analysisSourceText().trimmed().isEmpty(), 10000);
        controller.setValueFilterId(QStringLiteral("0X321"));
        controller.setTimingFilterSeverity(QStringLiteral("ERR"));
        controller.setAlarmFilterText(QStringLiteral("high"));

        controller.playReplay(1000.0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.replayPlaying(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.replayValueMarkerCount() > 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.replayAlarmMarkerCount() > 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.replayIssueMarkers().isEmpty(), 10000);
        QVERIFY(controller.seekReplayIssue(QStringLiteral("value"), 1));
        QTRY_COMPARE(controller.selectedValueId(), QStringLiteral("0X321"));
        QVERIFY(controller.seekReplayId(QStringLiteral("0X321"), 1));
        QTRY_COMPARE(controller.valueFilterId(), QStringLiteral("0X321"));

        const QString snapshotDir = tempDir.path() + QStringLiteral("/snapshot");
        QVERIFY(QDir().mkpath(snapshotDir));
        const QString jsonPath = snapshotDir + QStringLiteral("/export_snapshot.json");
        const QString markdownPath = snapshotDir + QStringLiteral("/export_snapshot.md");
        controller.exportAnalysisSnapshot(jsonPath);
        controller.exportAnalysisSnapshot(markdownPath);

        const QString summaryPath = snapshotDir + QStringLiteral("/export_snapshot.summary.md");
        QVERIFY(QFileInfo::exists(jsonPath));
        QVERIFY(QFileInfo::exists(summaryPath));
        QVERIFY(QFileInfo::exists(markdownPath));

        const QJsonDocument exportedJson = QJsonDocument::fromJson(readUtf8File(jsonPath).toUtf8());
        QVERIFY(exportedJson.isObject());
        const QJsonObject root = exportedJson.object();
        const QJsonObject context = root.value(QStringLiteral("context")).toObject();
        const QJsonObject counts = root.value(QStringLiteral("counts")).toObject();

        QCOMPARE(root.value(QStringLiteral("analysis_source")).toString(), controller.analysisSourceText());
        QCOMPARE(root.value(QStringLiteral("analysis_context_text")).toString(), controller.analysisContextText());
        QCOMPARE(root.value(QStringLiteral("active_view_state_summary")).toString(), controller.activeViewStateSummary());
        QCOMPARE(root.value(QStringLiteral("replay_cursor_summary")).toString(), controller.replayCursorSummary());
        QCOMPARE(root.value(QStringLiteral("session_summary")).toString(), controller.sessionSummary());
        QCOMPARE(root.value(QStringLiteral("operator_headline")).toString(), controller.operatorHeadline());
        QCOMPARE(root.value(QStringLiteral("operator_action_text")).toString(), controller.operatorActionText());
        QCOMPARE(root.value(QStringLiteral("default_snapshot_directory")).toString(), controller.defaultSnapshotDirectory());
        QCOMPARE(root.value(QStringLiteral("replay_path")).toString(), controller.replayPath());
        QCOMPARE(context.value(QStringLiteral("analysis_source_text")).toString(), controller.analysisSourceText());
        QCOMPARE(context.value(QStringLiteral("selected_value_id")).toString(), controller.selectedValueId());
        QCOMPARE(counts.value(QStringLiteral("replay_value_markers")).toInt(), controller.replayValueMarkerCount());
        QCOMPARE(counts.value(QStringLiteral("replay_alarm_markers")).toInt(), controller.replayAlarmMarkerCount());
        QCOMPARE(root.value(QStringLiteral("replay_issue_markers")).toArray().size(), controller.replayIssueMarkers().size());
        QVERIFY(!root.value(QStringLiteral("operator_headline")).toString().trimmed().isEmpty());
        QVERIFY(!root.value(QStringLiteral("operator_action_text")).toString().trimmed().isEmpty());

        const QString markdown = readUtf8File(markdownPath);
        const QString summaryMarkdown = readUtf8File(summaryPath);
        QVERIFY(markdown.contains(QStringLiteral("# CAN Monitor Analysis Snapshot")));
        QVERIFY(markdown.contains(QStringLiteral("- analysis_source: ") + controller.analysisSourceText()));
        QVERIFY(markdown.contains(QStringLiteral("- analysis_context: ") + controller.analysisContextText()));
        QVERIFY(markdown.contains(QStringLiteral("- operator_headline: ") + controller.operatorHeadline()));
        QVERIFY(markdown.contains(QStringLiteral("- operator_action: ") + controller.operatorActionLevel() + QStringLiteral(" / ") + controller.operatorActionText()));
        QVERIFY(markdown.contains(QStringLiteral("- replay_cursor_summary: ") + controller.replayCursorSummary()));
        QVERIFY(markdown.contains(QStringLiteral("- session_summary: ") + controller.sessionSummary()));

        QVERIFY(summaryMarkdown.contains(QStringLiteral("- analysis_source: ") + controller.analysisSourceText()));
        QVERIFY(summaryMarkdown.contains(QStringLiteral("- operator_headline: ") + controller.operatorHeadline()));
        QVERIFY(summaryMarkdown.contains(QStringLiteral("- operator_action: ") + controller.operatorActionLevel() + QStringLiteral(" / ") + controller.operatorActionText()));
    }
};

QTEST_GUILESS_MAIN(AppControllerExportSnapshotTest)

#include "test_app_controller_export_snapshot.moc"
