#include "AppController.h"
#include "TypedTransportParser.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <algorithm>

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

void appendU16Le(QByteArray& out, quint16 value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendU32Le(QByteArray& out, quint32 value) {
    for (int byte = 0; byte < 4; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

void appendU64Le(QByteArray& out, quint64 value) {
    for (int byte = 0; byte < 8; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

QByteArray typedFrame(TypedRecordType type, quint16 seq, const QByteArray& payload) {
    QByteArray frame;
    frame.reserve(int(kTypedTransportFrameOverhead + payload.size()));
    frame.append(char(kTypedTransportSof0));
    frame.append(char(kTypedTransportSof1));
    frame.append(char(kTypedTransportVersion));
    frame.append(char(static_cast<quint8>(type)));
    frame.append(char(0));
    appendU16Le(frame, seq);
    appendU16Le(frame, quint16(payload.size()));
    frame.append(payload);
    const auto* crcStart = reinterpret_cast<const quint8*>(frame.constData() + 2);
    appendU16Le(frame, TypedTransportParser::crc16Ccitt(crcStart, frame.size() - 2));
    return frame;
}

QByteArray typedCanRawFrame(TypedRecordType type, quint16 seq, quint64 monoUs, quint32 canId, quint8 bus, const QByteArray& data) {
    QByteArray payload;
    appendU64Le(payload, monoUs);
    appendU32Le(payload, canId);
    const int dlc = std::min<int>(data.size(), 8);
    payload.append(char(dlc & 0x0F));
    payload.append(char(bus));
    for (int index = 0; index < 8; ++index) {
        payload.append(index < dlc ? data.at(index) : char(0));
    }
    appendU32Le(payload, seq);
    appendU32Le(payload, 0);
    return typedFrame(type, seq, payload);
}

QString colorForSeriesKey(const QVariantList& series, const QString& key) {
    for (const QVariant& item : series) {
        const QVariantMap row = item.toMap();
        if (row.value(QStringLiteral("key")).toString() == key) {
            return row.value(QStringLiteral("color")).toString();
        }
    }
    return {};
}

QByteArray typedBoardHealthFrame(quint16 seq,
                                 quint64 monoUs,
                                 quint32 canRxTotal,
                                 quint32 droppedTotal,
                                 quint32 fifoOverflowTotal) {
    QByteArray payload;
    appendU64Le(payload, monoUs);
    appendU32Le(payload, canRxTotal);
    appendU32Le(payload, droppedTotal);
    appendU32Le(payload, fifoOverflowTotal);
    appendU32Le(payload, 11);
    appendU32Le(payload, 0);
    appendU32Le(payload, 0);
    appendU32Le(payload, 0);
    appendU64Le(payload, 0);
    payload.append(char(1));
    payload.append(char(0));
    payload.append(char(1));
    payload.append(char(0x0E));
    appendU32Le(payload, 0);
    return typedFrame(TypedRecordType::BoardHealth, seq, payload);
}

QByteArray typedBoardEventFrame(quint16 seq, quint64 monoUs, quint16 code, quint16 detail, quint32 counter) {
    QByteArray payload;
    appendU64Le(payload, monoUs);
    appendU16Le(payload, code);
    appendU16Le(payload, detail);
    appendU32Le(payload, counter);
    return typedFrame(TypedRecordType::BoardEvent, seq, payload);
}

QByteArray typedCapabilityFrame(quint16 seq, quint64 monoUs) {
    QByteArray payload;
    appendU64Le(payload, monoUs);
    payload.append(char(1));
    payload.append(char(3));
    payload.append(char(0));
    payload.append(char(1));
    appendU32Le(payload, 1024);
    appendU32Le(payload, 2048);
    appendU32Le(payload, 300000);
    payload.append(char(1));
    payload.append(char(1));
    payload.append(char(0));
    payload.append(char(0));
    payload.append(char(0));
    payload.append(char(1));
    payload.append(char(1));
    payload.append(char(0));
    payload.append(char(0));
    payload.append(char(0));
    payload.append(char(0));
    payload.append(char(0));
    payload.append(char(2));
    payload.append(char(20));
    appendU16Le(payload, 0);
    auto appendBus = [&payload](quint8 busId, quint8 backend, quint8 termination, quint8 isolation) {
        payload.append(char(busId));
        payload.append(char(0));
        payload.append(char(backend));
        payload.append(char(backend));
        payload.append(char(1));
        payload.append(char(1));
        payload.append(char(1));
        payload.append(char(1));
        payload.append(char(0));
        payload.append(char(8));
        appendU32Le(payload, 500000);
        appendU32Le(payload, 0);
        payload.append(char(termination));
        payload.append(char(isolation));
    };
    appendBus(0, 1, 3, 1);
    appendBus(1, 2, 0, 0);
    return typedFrame(TypedRecordType::Capability, seq, payload);
}

void writeTypedIndexEntry(QFile& indexFile, quint64 offset, quint64 monoUs, const QByteArray& frame) {
    QByteArray entry;
    entry.reserve(24);
    appendU64Le(entry, offset);
    appendU64Le(entry, monoUs);
    entry.append(frame.at(3));
    entry.append(frame.at(4));
    appendU16Le(entry, quint16(quint8(frame.at(5))) | quint16(quint8(frame.at(6)) << 8));
    appendU16Le(entry, quint16(quint8(frame.at(7))) | quint16(quint8(frame.at(8)) << 8));
    appendU16Le(entry, 0);
    indexFile.write(entry);
}

QString writeModelFixture(const QString& rootPath) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("can-monitor-model-pack.v1"));
    root.insert(QStringLiteral("model_key"), QStringLiteral("app_controller_replay_fixture"));
    root.insert(QStringLiteral("model_name"), QStringLiteral("Replay Flow Fixture"));
    root.insert(QStringLiteral("model_version"), QStringLiteral("2026-04-17"));
    root.insert(QStringLiteral("vendor"), QStringLiteral("Codex"));

    QJsonObject rule;
    rule.insert(QStringLiteral("id"), QStringLiteral("0x321"));
    rule.insert(QStringLiteral("name_en"), QStringLiteral("REPLAY_FLOW_FIXTURE"));
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

    QJsonObject signal2 = signal;
    signal2.insert(QStringLiteral("name"), QStringLiteral("Motor Current"));
    signal2.insert(QStringLiteral("byte_index_1based"), 2);
    signal2.insert(QStringLiteral("description"), QStringLiteral("fixture current"));
    signal2.insert(QStringLiteral("unit"), QStringLiteral("A"));
    signal2.insert(QStringLiteral("alarm_mode"), QStringLiteral("none"));
    signal2.remove(QStringLiteral("warn_max"));
    signal2.remove(QStringLiteral("err_max"));
    signal2.remove(QStringLiteral("alarm_severity"));
    signal2.remove(QStringLiteral("alarm_message"));

    QJsonObject message;
    message.insert(QStringLiteral("id"), QStringLiteral("0x321"));
    message.insert(QStringLiteral("name"), QStringLiteral("Replay Flow Frame"));
    message.insert(QStringLiteral("signals"), QJsonArray{signal, signal2});

    root.insert(QStringLiteral("rules"), QJsonArray{rule});
    root.insert(QStringLiteral("messages"), QJsonArray{message});

    const QString path = rootPath + QStringLiteral("/replay_flow_model.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return path;
}

QString writeReplayFixture(const QString& rootPath) {
    const QString path = rootPath + QStringLiteral("/replay_flow.bin");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(framePacket(100000, 0x321, 0, 1, QByteArray::fromHex("2800000000000000")));
    file.write(framePacket(260000, 0x321, 0, 2, QByteArray::fromHex("4600000000000000")));
    file.close();
    return path;
}

QString writeTypedReplaySession(const QString& rootPath) {
    const QString sessionDir = rootPath + QStringLiteral("/typed_flow.typed");
    QDir().mkpath(sessionDir);

    QFile meta(sessionDir + QStringLiteral("/session.meta.json"));
    if (!meta.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    QJsonObject metaObj;
    metaObj.insert(QStringLiteral("format"), QStringLiteral("typed-evidence-stream-v1"));
    metaObj.insert(QStringLiteral("created_local"), QStringLiteral("2026-04-10T14:27:49.032"));
    metaObj.insert(QStringLiteral("stream_file"), QStringLiteral("capture.stream"));
    meta.write(QJsonDocument(metaObj).toJson(QJsonDocument::Compact));
    meta.close();

    QFile stream(sessionDir + QStringLiteral("/capture.stream"));
    if (!stream.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};

    QFile index(sessionDir + QStringLiteral("/capture.index"));
    if (!index.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};

    auto writeFrame = [&stream, &index](const QByteArray& frame, quint64 monoUs) {
        const quint64 offset = quint64(stream.pos());
        stream.write(frame);
        writeTypedIndexEntry(index, offset, monoUs, frame);
    };
    writeFrame(typedCanRawFrame(TypedRecordType::CanRxRaw, 1, 100000, 0x321, 0, QByteArray::fromHex("280000")), 100000);
    writeFrame(typedCanRawFrame(TypedRecordType::CanTxRaw, 2, 120000, 0x510, 1, QByteArray::fromHex("4000000000000000")), 120000);
    writeFrame(typedCanRawFrame(TypedRecordType::CanRxRaw, 3, 260000, 0x321, 0, QByteArray::fromHex("4600000000")), 260000);
    writeFrame(typedBoardHealthFrame(4, 300000, 100, 0, 2), 300000);
    writeFrame(typedBoardHealthFrame(5, 400000, 120, 0, 5), 400000);
    writeFrame(typedBoardEventFrame(6, 420000, 9, 1568, 1), 420000);
    writeFrame(typedCapabilityFrame(7, 440000), 440000);
    index.close();
    stream.close();

    QFile events(sessionDir + QStringLiteral("/events.jsonl"));
    if (!events.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return {};
    events.write("{\"event\":\"finalized\",\"record_count\":7}\n");
    events.close();
    return sessionDir;
}

QString writePartialTypedReplaySession(const QString& rootPath) {
    const QString sessionDir = rootPath + QStringLiteral("/typed_partial.typed");
    QDir().mkpath(sessionDir);

    QFile meta(sessionDir + QStringLiteral("/session.meta.json.part"));
    if (!meta.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    meta.write("{\"format\":\"typed-evidence-stream-v1\",\"stream_file\":\"capture.stream\"}");
    meta.close();

    QFile index(sessionDir + QStringLiteral("/capture.index.part"));
    if (!index.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    index.write(QByteArray(24, char(0)));
    index.close();

    QFile events(sessionDir + QStringLiteral("/events.jsonl.part"));
    if (!events.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return {};
    events.write("{\"event\":\"interrupted\"}\n");
    events.close();

    QFile stream(sessionDir + QStringLiteral("/capture.stream.part"));
    if (!stream.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    stream.write(typedCanRawFrame(TypedRecordType::CanRxRaw, 1, 100000, 0x321, 0, QByteArray::fromHex("280000")));
    stream.write(typedCanRawFrame(TypedRecordType::CanRxRaw, 2, 200000, 0x321, 0, QByteArray::fromHex("460000")).left(5));
    stream.close();
    return sessionDir;
}

} // namespace

class AppControllerReplayFlowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void replayFlowGeneratesIssueMarkersAndSupportsIssueSeek() {
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

        QTRY_VERIFY(controller.modelActive());
        QCOMPARE(controller.rulesCount(), 1);
        QCOMPARE(controller.signalDbMessageCount(), 1);

        controller.loadReplay(replayPath);
        QTRY_VERIFY(controller.replayLoaded());
        QTRY_COMPARE(controller.replayFrameCount(), 2);
        QVERIFY(controller.replayPath().endsWith(QStringLiteral("replay_flow.bin")));

        QVERIFY(controller.jumpReplayToFrameIndex(1, QStringLiteral("test replay jump")));
        QTRY_VERIFY(!controller.replayRebuilding());
        QTRY_COMPARE(controller.replayCurrentIndex(), 1);
        QTRY_COMPARE(controller.replayObservedIdCount(), 1);
        QTRY_VERIFY(controller.timingModel()->rowCount() > 0);
        const QVariantMap timingTop = controller.timingModel()->get(0);
        QCOMPARE(timingTop.value(QStringLiteral("idText")).toString(), QStringLiteral("0X321"));
        QVERIFY(!timingTop.value(QStringLiteral("reason")).toString().trimmed().isEmpty());

        controller.playReplay(1000.0);
        QTRY_VERIFY(!controller.replayPlaying());
        QTRY_VERIFY(controller.replayValueMarkerCount() > 0);
        QTRY_VERIFY(controller.replayAlarmMarkerCount() > 0);
        QTRY_VERIFY(!controller.replayIssueMarkers().isEmpty());
        QVERIFY(controller.replayAnalysisHeld());

        QVERIFY(controller.seekReplayIssue(QStringLiteral("value"), 1));
        QTRY_COMPARE(controller.selectedValueId(), QStringLiteral("0X321"));
        QVERIFY(controller.seekReplayId(QStringLiteral("0X321"), 1));
        QTRY_COMPARE(controller.valueFilterId(), QStringLiteral("0X321"));
    }

    void replayOpenDirectoryCachesLastLoadedPath() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QByteArray appDataPath = QDir::toNativeSeparators(tempDir.path()).toUtf8();
        qputenv("APPDATA", appDataPath);
        qputenv("LOCALAPPDATA", appDataPath);

        const QString replayPath = writeReplayFixture(tempDir.path());
        QVERIFY(!replayPath.isEmpty());

        AppController controller;
        controller.clearSavedSession();
        QVERIFY(controller.replayOpenDirectory().contains(QStringLiteral("replay_data/logs")));

        controller.loadReplay(replayPath);
        QCOMPARE(QDir::cleanPath(controller.replayOpenDirectory()), QDir::cleanPath(tempDir.path()));

        const QString sessionDir = writeTypedReplaySession(tempDir.path());
        QVERIFY(!sessionDir.isEmpty());
        controller.loadReplay(sessionDir);
        QCOMPARE(QDir::cleanPath(controller.replayOpenDirectory()), QDir::cleanPath(sessionDir));
    }

    void graphOverviewDeselectReusesFullRangeCache() {
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
        QTRY_VERIFY(controller.modelActive());

        QStringList rawKeys;
        for (const QVariant& item : controller.graphCatalog()) {
            const QVariantMap row = item.toMap();
            if (row.value(QStringLiteral("mode")).toString() != QStringLiteral("raw")) continue;
            rawKeys << row.value(QStringLiteral("key")).toString();
            if (rawKeys.size() >= 2) break;
        }
        QVERIFY2(rawKeys.size() >= 2, "fixture must expose at least two raw graph signals");

        controller.loadReplay(replayPath);
        QTRY_VERIFY(controller.replayLoaded());

        controller.setGraphSelectedKeys(rawKeys.mid(0, 2));
        QTRY_VERIFY_WITH_TIMEOUT(controller.graphOverviewReady(), 5000);
        QCOMPARE(controller.graphOverviewSeries().size(), 2);
        QVERIFY(!controller.graphOverviewBuilding());
        const QString firstColor = colorForSeriesKey(controller.graphOverviewSeries(), rawKeys.first());
        const QString secondColor = colorForSeriesKey(controller.graphOverviewSeries(), rawKeys.at(1));
        QVERIFY(!firstColor.isEmpty());
        QVERIFY(!secondColor.isEmpty());

        controller.setGraphSelectedKeys(QStringList{rawKeys.first()});
        QVERIFY(!controller.graphOverviewBuilding());
        QTRY_VERIFY_WITH_TIMEOUT(controller.graphOverviewReady(), 1000);
        QCOMPARE(controller.graphOverviewSeries().size(), 1);
        QCOMPARE(colorForSeriesKey(controller.graphOverviewSeries(), rawKeys.first()), firstColor);

        controller.setGraphSelectedKeys(QStringList{rawKeys.at(1), rawKeys.first()});
        QTRY_VERIFY_WITH_TIMEOUT(controller.graphOverviewReady(), 1000);
        QCOMPARE(controller.graphOverviewSeries().size(), 2);
        QCOMPARE(colorForSeriesKey(controller.graphOverviewSeries(), rawKeys.first()), firstColor);
        QCOMPARE(colorForSeriesKey(controller.graphOverviewSeries(), rawKeys.at(1)), secondColor);

        const QVariantList detailSeries = controller.graphOverviewDetailSeries(0.0, 300.0);
        QCOMPARE(detailSeries.size(), 2);
        QCOMPARE(colorForSeriesKey(detailSeries, rawKeys.first()), firstColor);
        QCOMPARE(colorForSeriesKey(detailSeries, rawKeys.at(1)), secondColor);
    }

    void typedReplaySessionLoadsCanRxFramesOnly() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QByteArray appDataPath = QDir::toNativeSeparators(tempDir.path()).toUtf8();
        qputenv("APPDATA", appDataPath);
        qputenv("LOCALAPPDATA", appDataPath);

        const QString sessionDir = writeTypedReplaySession(tempDir.path());
        QVERIFY(!sessionDir.isEmpty());

        AppController controller;
        controller.clearSavedSession();
        controller.clearFrames();

        controller.loadReplay(sessionDir);
        QTRY_VERIFY(controller.replayLoaded());
        QTRY_COMPARE(controller.replayFrameCount(), 2);
        QVERIFY(controller.replayPath().endsWith(QStringLiteral("typed_flow.typed")));
        QCOMPARE(controller.replayTypedCaptureState(), QStringLiteral("FINALIZED"));
        QVERIFY(controller.replayTypedDiagnosticsSummary().contains(QStringLiteral("FINALIZED")));
        QVERIFY(controller.replayTypedDiagnosticsSummary().contains(QStringLiteral("index 7")));
        QVERIFY(controller.replayTypedDiagnosticsSummary().contains(QStringLiteral("events 1")));
        QVERIFY(!controller.replayTypedDiagnostics().isEmpty());

        QMap<QString, QString> diagnosticRows;
        QMap<QString, QString> diagnosticLevels;
        QMap<QString, QString> diagnosticLabels;
        QMap<QString, QString> diagnosticNotes;
        for (const QVariant& item : controller.replayTypedDiagnostics()) {
            const QVariantMap row = item.toMap();
            const QString key = row.value(QStringLiteral("key")).toString();
            diagnosticRows.insert(key, row.value(QStringLiteral("value")).toString());
            diagnosticLevels.insert(key, row.value(QStringLiteral("level")).toString());
            diagnosticLabels.insert(key, row.value(QStringLiteral("label")).toString());
            diagnosticNotes.insert(key, row.value(QStringLiteral("note")).toString());
        }
        QVERIFY(diagnosticRows.value(QStringLiteral("operator_verdict")).contains(QStringLiteral("재생 근거 정상")));
        QCOMPARE(diagnosticLevels.value(QStringLiteral("operator_verdict")), QStringLiteral("OK"));
        QCOMPARE(diagnosticLabels.value(QStringLiteral("operator_verdict")), QStringLiteral("판정"));
        QVERIFY(diagnosticNotes.value(QStringLiteral("operator_verdict")).contains(QStringLiteral("CAN_RX projection")));
        QVERIFY(diagnosticRows.value(QStringLiteral("can_bus")).contains(QStringLiteral("bus0=2")));
        QVERIFY(diagnosticRows.value(QStringLiteral("can_bus")).contains(QStringLiteral("bus1=1")));
        QVERIFY(diagnosticRows.value(QStringLiteral("can_dlc")).contains(QStringLiteral("dlc3=1")));
        QVERIFY(diagnosticRows.value(QStringLiteral("can_dlc")).contains(QStringLiteral("dlc5=1")));
        QVERIFY(diagnosticRows.value(QStringLiteral("can_dlc_verdict")).contains(QStringLiteral("Variable DLC preserved")));
        QCOMPARE(diagnosticLevels.value(QStringLiteral("can_dlc_verdict")), QStringLiteral("OK"));
        QVERIFY(diagnosticRows.value(QStringLiteral("can_rx_projection")).contains(QStringLiteral("2 replay frame")));
        QCOMPARE(diagnosticLevels.value(QStringLiteral("can_rx_projection")), QStringLiteral("OK"));
        QVERIFY(diagnosticRows.value(QStringLiteral("timeline")).contains(QStringLiteral("duration 340.0 ms")));
        QVERIFY(diagnosticRows.value(QStringLiteral("index")).contains(QStringLiteral("entries 7")));
        QVERIFY(diagnosticRows.value(QStringLiteral("index")).contains(QStringLiteral("mismatch 0")));
        QVERIFY(diagnosticRows.value(QStringLiteral("meta")).contains(QStringLiteral("typed-evidence-stream-v1")));
        QVERIFY(diagnosticRows.value(QStringLiteral("events")).contains(QStringLiteral("finalized")));
        QVERIFY(diagnosticRows.value(QStringLiteral("board_health")).contains(QStringLiteral("fifo_overflow +3")));
        QVERIFY(diagnosticRows.value(QStringLiteral("board_events")).contains(QStringLiteral("MCP2515_ERROR=1")));
        QVERIFY(diagnosticRows.value(QStringLiteral("capability_bus")).contains(QStringLiteral("bus0 MCP2515")));

        QVERIFY(controller.jumpReplayToFrameIndex(1, QStringLiteral("typed replay jump")));
        QTRY_VERIFY(!controller.replayRebuilding());
        QTRY_COMPARE(controller.replayCurrentIndex(), 1);
        QTRY_COMPARE(controller.replayObservedIdCount(), 1);
    }

    void partialTypedReplaySessionLoadsAndReportsInterruptedCapture() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QByteArray appDataPath = QDir::toNativeSeparators(tempDir.path()).toUtf8();
        qputenv("APPDATA", appDataPath);
        qputenv("LOCALAPPDATA", appDataPath);

        const QString sessionDir = writePartialTypedReplaySession(tempDir.path());
        QVERIFY(!sessionDir.isEmpty());

        AppController controller;
        controller.clearSavedSession();
        controller.clearFrames();

        controller.loadReplay(sessionDir);
        QTRY_VERIFY(controller.replayLoaded());
        QTRY_COMPARE(controller.replayFrameCount(), 1);
        QCOMPARE(controller.replayTypedCaptureState(), QStringLiteral("PARTIAL"));
        QVERIFY(controller.replayTypedDiagnosticsSummary().contains(QStringLiteral("PARTIAL")));
        QVERIFY(controller.replayTypedDiagnosticsSummary().contains(QStringLiteral("events 1")));

        bool foundFaultRow = false;
        bool foundPartIndexRow = false;
        bool foundOperatorWarn = false;
        bool foundFirstFault = false;
        for (const QVariant& item : controller.replayTypedDiagnostics()) {
            const QVariantMap row = item.toMap();
            if (row.value(QStringLiteral("key")).toString() == QStringLiteral("faults") &&
                row.value(QStringLiteral("value")).toString().contains(QStringLiteral("tail"))) {
                foundFaultRow = true;
            }
            if (row.value(QStringLiteral("key")).toString() == QStringLiteral("index") &&
                row.value(QStringLiteral("value")).toString().contains(QStringLiteral("part entries 1"))) {
                foundPartIndexRow = true;
            }
            if (row.value(QStringLiteral("key")).toString() == QStringLiteral("operator_verdict") &&
                row.value(QStringLiteral("level")).toString() == QStringLiteral("ERR") &&
                row.value(QStringLiteral("value")).toString().contains(QStringLiteral("재생 근거 확인 필요"))) {
                foundOperatorWarn = true;
            }
            if (row.value(QStringLiteral("key")).toString() == QStringLiteral("first_fault") &&
                row.value(QStringLiteral("value")).toString().contains(QStringLiteral("incomplete_frame"))) {
                foundFirstFault = true;
            }
        }
        QVERIFY(foundFaultRow);
        QVERIFY(foundPartIndexRow);
        QVERIFY(foundOperatorWarn);
        QVERIFY(foundFirstFault);
    }
};

QTEST_GUILESS_MAIN(AppControllerReplayFlowTest)

#include "test_app_controller_replay_flow.moc"
