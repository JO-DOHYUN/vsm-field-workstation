#include "FilePersistence.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class FilePersistenceTest : public QObject {
    Q_OBJECT

private slots:
    void writesJsonAtomicallyAndReplacesExisting() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("snapshot.json"));

        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Truncate));
        seed.write("stale");
        seed.close();

        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("turn81"));
        root.insert(QStringLiteral("count"), 3);

        QString error;
        QVERIFY2(FilePersistence::writeJsonAtomically(path, QJsonDocument(root), &error), qPrintable(error));

        QFile verify(path);
        QVERIFY(verify.open(QIODevice::ReadOnly));
        const QJsonDocument saved = QJsonDocument::fromJson(verify.readAll());
        QVERIFY(saved.isObject());
        QCOMPARE(saved.object().value(QStringLiteral("name")).toString(), QStringLiteral("turn81"));
        QCOMPARE(saved.object().value(QStringLiteral("count")).toInt(), 3);
    }

    void copiesFileAtomicallyWithoutRemovingSource() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("capture.bin"));
        const QString destination = dir.filePath(QStringLiteral("final/capture.bin"));

        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly | QIODevice::Truncate));
        input.write("1234567890");
        input.close();

        QString error;
        QVERIFY2(FilePersistence::copyFileAtomically(source, destination, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(QFileInfo::exists(destination));

        QFile srcVerify(source);
        QFile dstVerify(destination);
        QVERIFY(srcVerify.open(QIODevice::ReadOnly));
        QVERIFY(dstVerify.open(QIODevice::ReadOnly));
        QCOMPARE(srcVerify.readAll(), dstVerify.readAll());
    }

    void removeFileIfExistsIsIdempotent() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("temp.txt"));

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("temp");
        file.close();

        QString error;
        QVERIFY2(FilePersistence::removeFileIfExists(path, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY2(FilePersistence::removeFileIfExists(path, &error), qPrintable(error));
    }
};

QTEST_APPLESS_MAIN(FilePersistenceTest)

#include "test_file_persistence.moc"
