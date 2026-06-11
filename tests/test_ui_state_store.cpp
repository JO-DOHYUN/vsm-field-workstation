#include "SessionManager.h"
#include "UiStateStore.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class UiStateStoreTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsVersionedSnapshot() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString iniPath = dir.filePath(QStringLiteral("ui_state.ini"));

        SessionManager session(iniPath);
        UiStateStore store;
        AppUiState::Snapshot written;
        written.modelEnabled = true;
        written.modelBundled = false;
        written.modelPath = QStringLiteral("C:/models/turn77.json");
        written.timingSortMode = QStringLiteral("severity");
        written.timingSortDescending = true;
        written.valueSortMode = QStringLiteral("name");
        written.valueSortDescending = true;
        written.alarmSortMode = QStringLiteral("time");
        written.alarmSortDescending = false;
        written.liveViewState.timingFilterId = QStringLiteral("0x101");
        written.liveViewState.valueFilterName = QStringLiteral("Motor");
        written.liveViewState.selectedValueId = QStringLiteral("0x118");
        written.replayViewState.alarmFilterMessage = QStringLiteral("warn");
        written.liveFrameIdFilter = QStringLiteral("0x210");
        written.replayFrameIdFilter = QStringLiteral("0x211");
        written.liveFrameBusFilter = 0;
        written.replayFrameBusFilter = 1;
        written.replaySpeed = 8.0;
        written.replayLoop = true;
        written.liveUiPaused = true;
        written.logTargetDirectory = QStringLiteral("C:/field/logs");
        written.logTargetName = QStringLiteral("drive_one");

        store.save(session, written);

        SessionManager reloadedSession(iniPath);
        const AppUiState::Snapshot loaded = store.load(reloadedSession);
        QCOMPARE(loaded.modelEnabled, written.modelEnabled);
        QCOMPARE(loaded.modelBundled, written.modelBundled);
        QCOMPARE(loaded.modelPath, written.modelPath);
        QCOMPARE(loaded.timingSortMode, written.timingSortMode);
        QCOMPARE(loaded.timingSortDescending, written.timingSortDescending);
        QCOMPARE(loaded.valueSortMode, written.valueSortMode);
        QCOMPARE(loaded.valueSortDescending, written.valueSortDescending);
        QCOMPARE(loaded.alarmSortMode, written.alarmSortMode);
        QCOMPARE(loaded.alarmSortDescending, written.alarmSortDescending);
        QCOMPARE(loaded.liveViewState.timingFilterId, written.liveViewState.timingFilterId);
        QCOMPARE(loaded.liveViewState.valueFilterName, written.liveViewState.valueFilterName);
        QCOMPARE(loaded.liveViewState.selectedValueId, written.liveViewState.selectedValueId);
        QCOMPARE(loaded.replayViewState.alarmFilterMessage, written.replayViewState.alarmFilterMessage);
        QCOMPARE(loaded.liveFrameIdFilter, written.liveFrameIdFilter);
        QCOMPARE(loaded.replayFrameIdFilter, written.replayFrameIdFilter);
        QCOMPARE(loaded.liveFrameBusFilter, written.liveFrameBusFilter);
        QCOMPARE(loaded.replayFrameBusFilter, written.replayFrameBusFilter);
        QCOMPARE(loaded.replaySpeed, written.replaySpeed);
        QCOMPARE(loaded.replayLoop, written.replayLoop);
        QCOMPARE(loaded.liveUiPaused, written.liveUiPaused);
        QCOMPARE(loaded.logTargetDirectory, written.logTargetDirectory);
        QCOMPARE(loaded.logTargetName, written.logTargetName);

        QSettings raw(iniPath, QSettings::IniFormat);
        QCOMPARE(raw.value(QStringLiteral("ui_state/version")).toInt(), UiStateStore::kSchemaVersion);
        QVERIFY(!raw.value(QStringLiteral("ui_state/blob")).toString().trimmed().isEmpty());
    }

    void fallsBackToLegacyKeys() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString iniPath = dir.filePath(QStringLiteral("legacy_state.ini"));

        SessionManager session(iniPath);
        session.setValue(QStringLiteral("model/enabled"), false);
        session.setValue(QStringLiteral("model/useBundled"), false);
        session.setValue(QStringLiteral("model/path"), QStringLiteral("  C:/legacy/model.json  "));
        session.setValue(QStringLiteral("sort/timingMode"), QStringLiteral("severity"));
        session.setValue(QStringLiteral("sort/timingDesc"), true);
        session.setValue(QStringLiteral("filter/live/timingId"), QStringLiteral("0x301"));
        session.setValue(QStringLiteral("filter/live/valueName"), QStringLiteral("Speed"));
        session.setValue(QStringLiteral("filter/live/selectedValueId"), QStringLiteral("0x118"));
        session.setValue(QStringLiteral("frame/liveIdFilter"), QStringLiteral("0x401"));
        session.setValue(QStringLiteral("frame/liveBusFilter"), 1);
        session.setValue(QStringLiteral("frame/replayBusFilter"), 0);
        session.setValue(QStringLiteral("replay/speed"), 12.0);
        session.setValue(QStringLiteral("replay/loop"), true);
        session.setValue(QStringLiteral("ui/livePaused"), true);
        session.setValue(QStringLiteral("log/targetDirectory"), QStringLiteral("C:/legacy/logs"));
        session.setValue(QStringLiteral("log/targetName"), QStringLiteral("legacy_drive"));
        session.sync();

        UiStateStore store;
        const AppUiState::Snapshot loaded = store.load(session);
        QCOMPARE(loaded.modelEnabled, false);
        QCOMPARE(loaded.modelBundled, false);
        QCOMPARE(loaded.modelPath, QStringLiteral("C:/legacy/model.json"));
        QCOMPARE(loaded.timingSortMode, QStringLiteral("severity"));
        QCOMPARE(loaded.timingSortDescending, true);
        QCOMPARE(loaded.liveViewState.timingFilterId, QStringLiteral("0x301"));
        QCOMPARE(loaded.liveViewState.valueFilterName, QStringLiteral("Speed"));
        QCOMPARE(loaded.liveViewState.selectedValueId, QStringLiteral("0x118"));
        QCOMPARE(loaded.liveFrameIdFilter, QStringLiteral("0x401"));
        QCOMPARE(loaded.liveFrameBusFilter, 1);
        QCOMPARE(loaded.replayFrameBusFilter, 0);
        QCOMPARE(loaded.replaySpeed, 8.0);
        QCOMPARE(loaded.replayLoop, true);
        QCOMPARE(loaded.liveUiPaused, true);
        QCOMPARE(loaded.logTargetDirectory, QStringLiteral("C:/legacy/logs"));
        QCOMPARE(loaded.logTargetName, QStringLiteral("legacy_drive"));
    }

    void corruptVersionedSnapshotFallsBackToLegacy() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString iniPath = dir.filePath(QStringLiteral("corrupt_state.ini"));

        SessionManager session(iniPath);
        session.setValue(QStringLiteral("ui_state/version"), UiStateStore::kSchemaVersion);
        session.setValue(QStringLiteral("ui_state/blob"), QStringLiteral("{not-json"));
        session.setValue(QStringLiteral("model/enabled"), true);
        session.setValue(QStringLiteral("model/useBundled"), false);
        session.setValue(QStringLiteral("model/path"), QStringLiteral("C:/fallback/model.json"));
        session.setValue(QStringLiteral("replay/speed"), -7.0);
        session.sync();

        UiStateStore store;
        const AppUiState::Snapshot loaded = store.load(session);
        QCOMPARE(loaded.modelEnabled, true);
        QCOMPARE(loaded.modelBundled, false);
        QCOMPARE(loaded.modelPath, QStringLiteral("C:/fallback/model.json"));
        QCOMPARE(loaded.replaySpeed, 1.0);
    }
};

QTEST_APPLESS_MAIN(UiStateStoreTest)

#include "test_ui_state_store.moc"
