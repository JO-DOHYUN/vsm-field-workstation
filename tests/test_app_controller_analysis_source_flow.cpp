#include "AppController.h"

#include <QDir>
#include <QFile>
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
    root.insert(QStringLiteral("model_key"), QStringLiteral("analysis_source_fixture"));
    root.insert(QStringLiteral("model_name"), QStringLiteral("Analysis Source Fixture"));
    root.insert(QStringLiteral("model_version"), QStringLiteral("2026-04-17"));
    root.insert(QStringLiteral("vendor"), QStringLiteral("Codex"));

    QJsonObject rule;
    rule.insert(QStringLiteral("id"), QStringLiteral("0x321"));
    rule.insert(QStringLiteral("name_en"), QStringLiteral("ANALYSIS_SOURCE_FIXTURE"));
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
    message.insert(QStringLiteral("name"), QStringLiteral("Analysis Source Frame"));
    message.insert(QStringLiteral("signals"), QJsonArray{signal});

    root.insert(QStringLiteral("rules"), QJsonArray{rule});
    root.insert(QStringLiteral("messages"), QJsonArray{message});

    const QString path = rootPath + QStringLiteral("/analysis_source_model.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return path;
}

QString writeReplayFixture(const QString& rootPath) {
    const QString path = rootPath + QStringLiteral("/analysis_source_replay.bin");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(framePacket(100000, 0x321, 0, 1, QByteArray::fromHex("1000000000000000")));
    file.write(framePacket(200000, 0x321, 0, 2, QByteArray::fromHex("2000000000000000")));
    file.close();
    return path;
}

} // namespace

class AppControllerAnalysisSourceFlowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void preservesPerSourceFiltersAndSelectionAcrossReplayLiveTransitions() {
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

        controller.m_connected = true;
        controller.setValueFilterId(QStringLiteral("0X111"));
        controller.setTimingFilterSeverity(QStringLiteral("ERR"));
        controller.setAlarmFilterText(QStringLiteral("live-only"));
        controller.liveFrameView()->setIdFilter(QStringLiteral("0X111"));

        controller.loadReplay(replayPath);
        QTRY_VERIFY_WITH_TIMEOUT(controller.replayLoaded(), 10000);
        QCOMPARE(controller.replayAnalysisActive(), false);
        QVERIFY(controller.analysisSourceText().contains(QStringLiteral("라이브")));

        controller.setLiveUiPaused(true);
        QTRY_VERIFY_WITH_TIMEOUT(controller.replayAnalysisActive(), 10000);
        QCOMPARE(controller.replayAnalysisHeld(), false);
        QVERIFY(controller.analysisSourceText().contains(QStringLiteral("재생")));

        controller.setValueFilterId(QStringLiteral("0X321"));
        controller.setTimingFilterSeverity(QStringLiteral("WARN"));
        controller.setAlarmFilterText(QStringLiteral("replay-only"));
        controller.replayFrameView()->setIdFilter(QStringLiteral("0X321"));

        controller.setLiveUiPaused(false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.replayAnalysisActive(), 10000);
        QCOMPARE(controller.valueFilterId(), QStringLiteral("0X111"));
        QCOMPARE(controller.timingFilterSeverity(), QStringLiteral("ERR"));
        QCOMPARE(controller.alarmFilterText(), QStringLiteral("live-only"));
        QCOMPARE(controller.liveFrameView()->idFilter(), QStringLiteral("0X111"));
        QVERIFY(controller.analysisSourceText().contains(QStringLiteral("라이브")));
        QVERIFY(controller.activeViewStateSummary().contains(QStringLiteral("0X111")));

        controller.pauseReplay();
        QTRY_VERIFY_WITH_TIMEOUT(controller.replayAnalysisActive(), 10000);
        QCOMPARE(controller.replayAnalysisHeld(), true);
        QCOMPARE(controller.valueFilterId(), QStringLiteral("0X321"));
        QCOMPARE(controller.timingFilterSeverity(), QStringLiteral("WARN"));
        QCOMPARE(controller.alarmFilterText(), QStringLiteral("replay-only"));
        QCOMPARE(controller.replayFrameView()->idFilter(), QStringLiteral("0X321"));
        QVERIFY(controller.analysisSourceText().contains(QStringLiteral("재생")));
        QVERIFY(controller.activeViewStateSummary().contains(QStringLiteral("0X321")));

        controller.useLiveAnalysis();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.replayAnalysisActive(), 10000);
        QCOMPARE(controller.replayAnalysisHeld(), false);
        QCOMPARE(controller.valueFilterId(), QStringLiteral("0X111"));
        QCOMPARE(controller.timingFilterSeverity(), QStringLiteral("ERR"));
        QCOMPARE(controller.alarmFilterText(), QStringLiteral("live-only"));
        QCOMPARE(controller.liveFrameView()->idFilter(), QStringLiteral("0X111"));
    }
};

QTEST_GUILESS_MAIN(AppControllerAnalysisSourceFlowTest)

#include "test_app_controller_analysis_source_flow.moc"
