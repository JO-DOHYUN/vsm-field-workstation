#include "core/RuntimeProfile.h"

#include <QTest>

using namespace CanMonitorCore;

class RuntimeProfileTest : public QObject {
    Q_OBJECT

private slots:
    void passiveProductIsReadOnlyAndCdcSessionOnly() {
        const RuntimeProfile profile = passiveProductProfile();
        QCOMPARE(profile.key(), QStringLiteral("passive_product"));
        QCOMPARE(profile.kind(), RuntimeProfileKind::PassiveProduct);
        QCOMPARE(profile.impactState(), VehicleImpactState::ConfiguredPassive);
        const RuntimeTransportPolicy policy = profile.transportPolicy();
        QVERIFY(policy.serialOpenMode.testFlag(QIODevice::ReadOnly));
        QVERIFY(!policy.serialOpenMode.testFlag(QIODevice::WriteOnly));
        QVERIFY(policy.touchDtr);
        QVERIFY(policy.dtrAsserted);
        QVERIFY(policy.dtrSessionOnly);
        QVERIFY(!policy.touchRts);
        QVERIFY(!policy.hostTxEnabled);
        QVERIFY(!policy.controlCycleEnabled);
        QVERIFY(!policy.labGatewayEnabled);
        QVERIFY(!policy.serialWriteAllowed());
    }

    void fullInstrumentedIsExplicitlyActivePossible() {
        const RuntimeProfile profile = fullInstrumentedProfile();
        QCOMPARE(profile.key(), QStringLiteral("full_instrumented"));
        QCOMPARE(profile.kind(), RuntimeProfileKind::FullInstrumented);
        QCOMPARE(profile.impactState(), VehicleImpactState::ActivePossible);
        const RuntimeTransportPolicy policy = profile.transportPolicy();
        QVERIFY(policy.serialOpenMode.testFlag(QIODevice::ReadOnly));
        QVERIFY(policy.serialOpenMode.testFlag(QIODevice::WriteOnly));
        QVERIFY(policy.touchDtr);
        QVERIFY(policy.touchRts);
        QVERIFY(policy.hostTxEnabled);
        QVERIFY(policy.controlCycleEnabled);
        QVERIFY(policy.labGatewayEnabled);
        QVERIFY(policy.serialWriteAllowed());
    }

    void stringParserDefaultsUnknownToPassive() {
        bool ok = true;
        const RuntimeProfile unknown = runtimeProfileFromString(QStringLiteral("not-a-profile"), &ok);
        QVERIFY(!ok);
        QCOMPARE(unknown.key(), QStringLiteral("passive_product"));

        ok = false;
        const RuntimeProfile lab = runtimeProfileFromString(QStringLiteral("lab"), &ok);
        QVERIFY(ok);
        QCOMPARE(lab.key(), QStringLiteral("full_instrumented"));
    }
};

QTEST_MAIN(RuntimeProfileTest)
#include "test_runtime_profile.moc"
