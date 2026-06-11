#include "Recorder.h"
#include "SerialWorker.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QByteArray makeRaw20(quint8 seed) {
    QByteArray raw(20, Qt::Uninitialized);
    for (int index = 0; index < raw.size(); ++index) {
        raw[index] = char(seed + index);
    }
    return raw;
}

QString readUtf8File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

class RecorderFlowTest : public QObject {
    Q_OBJECT

private slots:
    void recorderWritesMetaBinAndModelSnapshot() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString binPath = dir.filePath(QStringLiteral("capture.bin"));
        const QString metaPath = dir.filePath(QStringLiteral("capture.meta.json"));
        const QString snapshotPath = dir.filePath(QStringLiteral("capture.model.json"));
        const QString sourceModelPath = dir.filePath(QStringLiteral("active_model.json"));

        QFile model(sourceModelPath);
        QVERIFY(model.open(QIODevice::WriteOnly | QIODevice::Truncate));
        model.write("{\"model\":\"turn77\"}");
        model.close();

        Recorder recorder;
        QString error;
        QVERIFY2(recorder.start(binPath, metaPath, snapshotPath, sourceModelPath, &error), qPrintable(error));

        recorder.append20(makeRaw20(1));
        recorder.append20(makeRaw20(21));
        recorder.flushPending(true);
        recorder.stop();

        QVERIFY(QFileInfo::exists(binPath));
        QVERIFY(QFileInfo::exists(metaPath));
        QVERIFY(QFileInfo::exists(snapshotPath));
        QCOMPARE(recorder.frameCount(), quint64(2));
        QCOMPARE(recorder.bytesWritten(), quint64(40));

        QFile bin(binPath);
        QVERIFY(bin.open(QIODevice::ReadOnly));
        QCOMPARE(bin.readAll().size(), 40);

        const QJsonDocument metaDoc = QJsonDocument::fromJson(readUtf8File(metaPath).toUtf8());
        QVERIFY(metaDoc.isObject());
        QCOMPARE(metaDoc.object().value(QStringLiteral("record_size")).toInt(), 20);
        QCOMPARE(metaDoc.object().value(QStringLiteral("format")).toString(), QStringLiteral("board-can-record-v1"));

        QCOMPARE(readUtf8File(snapshotPath), QStringLiteral("{\"model\":\"turn77\"}"));
        QVERIFY(recorder.lastError().isEmpty());
    }

    void recorderStartFailsWhenMetadataPathIsInvalid() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString binPath = dir.filePath(QStringLiteral("capture.bin"));
        const QString blockerPath = dir.filePath(QStringLiteral("meta_blocker"));
        QFile blocker(blockerPath);
        QVERIFY(blocker.open(QIODevice::WriteOnly | QIODevice::Truncate));
        blocker.write("x");
        blocker.close();

        Recorder recorder;
        QString error;
        QVERIFY(!recorder.start(binPath,
                                blockerPath + QStringLiteral("/capture.meta.json"),
                                QString(),
                                QString(),
                                &error));
        QVERIFY(!error.trimmed().isEmpty());
        QVERIFY(!QFileInfo::exists(binPath));
        QVERIFY(!recorder.isActive());
    }

    void recorderStopKeepsSnapshotCopyFailureInLastError() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString binPath = dir.filePath(QStringLiteral("capture.bin"));
        const QString metaPath = dir.filePath(QStringLiteral("capture.meta.json"));
        const QString snapshotPath = dir.filePath(QStringLiteral("capture.model.json"));
        const QString missingModelPath = dir.filePath(QStringLiteral("missing_model.json"));

        Recorder recorder;
        QString error;
        QVERIFY2(recorder.start(binPath, metaPath, snapshotPath, missingModelPath, &error), qPrintable(error));

        recorder.append20(makeRaw20(3));
        recorder.stop();

        QVERIFY(!recorder.lastError().trimmed().isEmpty());
        QVERIFY(!QFileInfo::exists(snapshotPath));
    }

    void serialWorkerSurfacesRecorderStopWarning() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString binPath = dir.filePath(QStringLiteral("capture.bin"));
        const QString metaPath = dir.filePath(QStringLiteral("capture.meta.json"));
        const QString snapshotPath = dir.filePath(QStringLiteral("capture.model.json"));
        const QString missingModelPath = dir.filePath(QStringLiteral("missing_model.json"));

        SerialWorker worker;
        QSignalSpy errorSpy(&worker, &SerialWorker::errorOccurred);
        QSignalSpy loggingStateSpy(&worker, &SerialWorker::loggingStateChanged);

        worker.setLogging(true, binPath, metaPath, snapshotPath, missingModelPath);
        worker.setLogging(false, QString(), QString(), QString(), QString());

        QVERIFY(loggingStateSpy.size() >= 2);
        QVERIFY(errorSpy.size() >= 1);
        QVERIFY(errorSpy.at(0).at(0).toString().contains(QStringLiteral("경고")));
    }
};

QTEST_MAIN(RecorderFlowTest)

#include "test_recorder_flow.moc"
