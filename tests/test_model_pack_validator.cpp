#include "ModelPack.h"
#include "ModelPackValidator.h"

#include <QFile>
#include <QJsonDocument>
#include <QtTest/QtTest>

namespace {

QJsonObject loadFixture(const QString& name) {
    QFile file(QStringLiteral(CAN_MONITOR_TEST_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

} // namespace

class ModelPackValidatorTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsValidFixture() {
        const QJsonObject root = loadFixture(QStringLiteral("model_pack_valid.json"));
        QVERIFY(!root.isEmpty());

        const QVector<CanModel::ValidationIssue> issues = CanModel::ModelPackValidator::validate(root);
        QVERIFY(!CanModel::ModelPackValidator::hasErrors(issues));

        CanModel::ModelPack pack;
        QString error;
        QVERIFY(CanModel::ModelPackLoader::loadObject(root, &pack, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(pack.rules.size(), 1);
        QCOMPARE(pack.messages.size(), 1);
        QVERIFY(pack.controlPolicy.declared);
        QCOMPARE(pack.controlPolicy.profileName, QStringLiteral("fixture-lab"));
        QCOMPARE(pack.controlPolicy.maxRpm, 3200);
        QCOMPARE(pack.controlPolicy.maxAbsSteeringDeg, 35.0);
        QCOMPARE(pack.controlPolicy.allowedBusRoles, QStringList{QStringLiteral("system")});
        QCOMPARE(pack.controlPolicy.busRoleRules.size(), 2);
        QVERIFY(pack.controlPolicy.busRoleRules.front().fingerprints.contains(0x120u));
    }

    void rejectsDuplicateSignalFixture() {
        const QJsonObject root = loadFixture(QStringLiteral("model_pack_invalid_duplicate_signal.json"));
        QVERIFY(!root.isEmpty());

        const QVector<CanModel::ValidationIssue> issues = CanModel::ModelPackValidator::validate(root);
        QVERIFY(CanModel::ModelPackValidator::hasErrors(issues));
        QVERIFY(CanModel::ModelPackValidator::summarize(issues).contains(QStringLiteral("signals.duplicate_name")));

        CanModel::ModelPack pack;
        QString error;
        QVERIFY(!CanModel::ModelPackLoader::loadObject(root, &pack, &error));
        QVERIFY(error.contains(QStringLiteral("signals.duplicate_name")));
    }

    void allowsReservedPlaceholderDuplicates() {
        const QJsonObject root = loadFixture(QStringLiteral("model_pack_reserved_duplicates_allowed.json"));
        QVERIFY(!root.isEmpty());

        const QVector<CanModel::ValidationIssue> issues = CanModel::ModelPackValidator::validate(root);
        QVERIFY(!CanModel::ModelPackValidator::hasErrors(issues));

        CanModel::ModelPack pack;
        QString error;
        QVERIFY(CanModel::ModelPackLoader::loadObject(root, &pack, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
};

QTEST_APPLESS_MAIN(ModelPackValidatorTest)

#include "test_model_pack_validator.moc"
