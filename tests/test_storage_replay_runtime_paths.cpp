#include "ReplayRuntime.h"
#include "SessionManager.h"
#include "StorageRuntime.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

class StorageReplayRuntimePathsTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void storageLogPlansUseProjectReplayData() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QByteArray appDataPath = QDir::toNativeSeparators(tempDir.path()).toUtf8();
        qputenv("APPDATA", appDataPath);
        qputenv("LOCALAPPDATA", appDataPath);

        const QString logDir = QDir::fromNativeSeparators(StorageRuntime::defaultLogDirectory());
        const QString snapshotDir = QDir::fromNativeSeparators(StorageRuntime::defaultSnapshotDirectory());
        QVERIFY(logDir.contains(QStringLiteral("/replay_data/logs")));
        QVERIFY(snapshotDir.contains(QStringLiteral("/replay_data/snapshots")));
        QVERIFY(!logDir.startsWith(QDir::fromNativeSeparators(tempDir.path())));

        const StorageRuntime::LogSessionPaths legacy =
            StorageRuntime::makeLegacyLogPaths(QStringLiteral("20260529_101112"), true);
        QVERIFY(!legacy.typedSession);
        QVERIFY(legacy.recordPath.endsWith(QStringLiteral("capture_20260529_101112.bin")));
        QVERIFY(legacy.metaPath.endsWith(QStringLiteral("capture_20260529_101112.meta.json")));
        QVERIFY(legacy.modelPath.endsWith(QStringLiteral("capture_20260529_101112.model.json")));
        QVERIFY(legacy.suggestedSavePath.endsWith(QStringLiteral("can_log_20260529_101112.bin")));
        QVERIFY(QFileInfo::exists(StorageRuntime::defaultTempLogDirectory()));

        const StorageRuntime::LogSessionPaths typed =
            StorageRuntime::makeTypedCapturePaths(QStringLiteral("20260529_101112"));
        QVERIFY(typed.typedSession);
        QVERIFY(typed.recordPath.endsWith(QStringLiteral("typed_capture_20260529_101112.typed")));
        QCOMPARE(typed.suggestedSavePath, typed.recordPath);

        const QString fieldDir = tempDir.path() + QStringLiteral("/field logs");
        const StorageRuntime::LogSessionPaths namedTyped =
            StorageRuntime::makeTypedCapturePaths(QStringLiteral("20260529_101112"), fieldDir, QStringLiteral(" 실차 bus0/bus1 <> "));
        QVERIFY(namedTyped.typedSession);
        QVERIFY(QDir::cleanPath(namedTyped.recordPath).startsWith(QDir::cleanPath(fieldDir)));
        QVERIFY(namedTyped.recordPath.endsWith(QStringLiteral("실차_bus0_bus1.typed")));

        const StorageRuntime::LogSessionPaths namedLegacy =
            StorageRuntime::makeLegacyLogPaths(QStringLiteral("20260529_101112"), false, fieldDir, QStringLiteral("drive check.bin"));
        QVERIFY(!namedLegacy.typedSession);
        QVERIFY(QDir::cleanPath(namedLegacy.suggestedSavePath).startsWith(QDir::cleanPath(fieldDir)));
        QVERIFY(namedLegacy.suggestedSavePath.endsWith(QStringLiteral("drive_check.bin")));

        const StorageRuntime::PendingSavePaths save =
            StorageRuntime::makePendingSavePaths(tempDir.path() + QStringLiteral("/saved/session_one"));
        QVERIFY(save.valid);
        QVERIFY(save.finalBin.endsWith(QStringLiteral("session_one.bin")));
        QVERIFY(save.finalMeta.endsWith(QStringLiteral("session_one.meta.json")));
        QVERIFY(save.finalModel.endsWith(QStringLiteral("session_one.model.json")));
    }

    void replayOpenDirectoryCacheLivesInReplayRuntime() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        SessionManager session(tempDir.path() + QStringLiteral("/session.ini"));
        ReplayRuntime runtime;
        QVERIFY(QDir::fromNativeSeparators(runtime.openDirectory(session)).contains(QStringLiteral("/replay_data/logs")));

        const QString replayPath = tempDir.path() + QStringLiteral("/drive.bin");
        QFile replayFile(replayPath);
        QVERIFY(replayFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        replayFile.write(QByteArray(20, char(0)));
        replayFile.close();

        const ReplayRuntime::LoadRequest fileRequest =
            runtime.prepareLoadRequest(QUrl::fromLocalFile(replayPath).toString(), session);
        QCOMPARE(QDir::cleanPath(fileRequest.normalizedPath), QDir::cleanPath(replayPath));
        QCOMPARE(QDir::cleanPath(fileRequest.openDirectory), QDir::cleanPath(tempDir.path()));
        QVERIFY(fileRequest.exists);
        QVERIFY(!fileRequest.typedContainer);

        ReplayRuntime restored;
        restored.restoreSession(session);
        QCOMPARE(QDir::cleanPath(restored.openDirectory(session)), QDir::cleanPath(tempDir.path()));

        const QString typedDir = tempDir.path() + QStringLiteral("/typed_capture.typed");
        QVERIFY(QDir().mkpath(typedDir));
        const ReplayRuntime::LoadRequest typedRequest = restored.prepareLoadRequest(typedDir, session);
        QVERIFY(typedRequest.exists);
        QVERIFY(typedRequest.typedContainer);
        QCOMPARE(QDir::cleanPath(restored.openDirectory(session)), QDir::cleanPath(typedDir));

        restored.clearSession(session);
        QVERIFY(QDir::fromNativeSeparators(restored.openDirectory(session)).contains(QStringLiteral("/replay_data/logs")));
    }
};

QTEST_GUILESS_MAIN(StorageReplayRuntimePathsTest)

#include "test_storage_replay_runtime_paths.moc"
