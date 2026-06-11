#include "TypedRecords.h"
#include "evidence/BoardConnectionState.h"
#include "evidence/BusRoleResolver.h"
#include "evidence/EvidenceRuntime.h"

#include <QtTest/QtTest>

namespace {

void appendU32(QByteArray& out, quint32 value) {
    for (int byte = 0; byte < 4; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

void appendU64(QByteArray& out, quint64 value) {
    for (int byte = 0; byte < 8; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

TypedRecord makeRecord(TypedRecordType type, const QByteArray& payload) {
    TypedRecord record;
    record.header.version = kTypedTransportVersion;
    record.header.recordType = static_cast<quint8>(type);
    record.header.payloadLength = quint16(payload.size());
    record.payload = payload;
    return record;
}

TypedCapabilityRecord capability(quint8 protocol = kTypedTransportVersion) {
    TypedCapabilityRecord out;
    out.monoUs = 100;
    out.protocolVersion = protocol;
    out.profileMajor = 1;
    out.profileMinor = 0;
    out.supportsCanRxRaw = true;
    out.supportsCanTxRaw = true;
    out.supportsBoardHealth = true;
    out.supportsBoardEvent = true;
    return out;
}

TypedBoardHealthRecord health(quint64 monoUs = 200, quint8 safetyState = 1, quint32 faultFlags = 0) {
    TypedBoardHealthRecord out;
    out.monoUs = monoUs;
    out.safetyState = safetyState;
    out.faultFlags = faultFlags;
    return out;
}

} // namespace

class EvidenceRuntimeFoundationTest : public QObject {
    Q_OBJECT

private slots:
    void decodesControlAckAsAcceptanceOnlyEvidence() {
        QByteArray payload;
        appendU64(payload, 123456);
        appendU32(payload, 0xAABBCCDD);
        payload.append(char(1)); // accepted queued
        payload.append(char(0)); // ok reason
        payload.append(char(3)); // target bus
        payload.append(char(8)); // dlc flags
        appendU32(payload, 0x513);
        appendU32(payload, 7); // accepted/request counter
        appendU32(payload, 2); // rejected total

        const auto ack = decodeTypedControlAck(makeRecord(TypedRecordType::ControlAck, payload));
        QVERIFY(ack.has_value());
        QCOMPARE(ack->monoUs, quint64(123456));
        QCOMPARE(ack->commandId, quint32(0xAABBCCDD));
        QCOMPARE(ack->status, quint8(1));
        QCOMPARE(ack->reason, quint8(0));
        QCOMPARE(ack->targetBus, quint8(3));
        QCOMPARE(ack->targetDlcFlags, quint8(8));
        QCOMPARE(ack->targetCanId, quint32(0x513));
        QVERIFY(!ack->targetExtended);
        QVERIFY(!ack->targetRtr);
        QCOMPARE(ack->counter, quint32(7));
        QCOMPARE(ack->rejectedTotal, quint32(2));
    }

    void decodesCapabilityBusDescriptorsForControlGate() {
        QByteArray payload(112, char(0));
        auto setU16 = [&payload](int offset, quint16 value) {
            payload[offset] = char(value & 0xFF);
            payload[offset + 1] = char((value >> 8) & 0xFF);
        };
        auto setU32 = [&payload](int offset, quint32 value) {
            for (int byte = 0; byte < 4; ++byte) payload[offset + byte] = char((value >> (byte * 8)) & 0xFF);
        };
        auto setU64 = [&payload](int offset, quint64 value) {
            for (int byte = 0; byte < 8; ++byte) payload[offset + byte] = char((value >> (byte * 8)) & 0xFF);
        };
        auto setBus = [&payload, &setU32](int offset, quint8 busId, quint8 backend, bool controlAllowed) {
            payload[offset + 0] = char(busId);
            payload[offset + 1] = char(busId == 0 ? 2 : 1);
            payload[offset + 2] = char(backend);
            payload[offset + 3] = char(busId == 0 ? 1 : 3);
            payload[offset + 4] = char(1);
            payload[offset + 5] = char(1);
            payload[offset + 6] = char(controlAllowed ? 1 : 0);
            payload[offset + 7] = char(1);
            payload[offset + 8] = char(0);
            payload[offset + 9] = char(8);
            setU32(offset + 10, 500000);
        };

        setU64(0, 777);
        payload[8] = char(kTypedTransportVersion);
        payload[9] = char(3);
        payload[10] = char(0);
        payload[11] = char(1);
        setU32(12, 1024);
        payload[24] = char(1);
        payload[25] = char(1);
        payload[29] = char(1);
        payload[30] = char(1);
        payload[36] = char(2);
        payload[37] = char(20);
        setU16(38, 0x0003);
        setBus(40, 0, 1, true);
        setBus(60, 1, 2, false);
        setU32(80, 0x000003E6);
        setU32(84, 0x00000C00);
        setU32(96, 0xAABBCCDD);
        setU16(100, 32);

        const auto capability = decodeTypedCapability(makeRecord(TypedRecordType::Capability, payload));
        QVERIFY(capability.has_value());
        QCOMPARE(capability->profileMajor, quint8(3));
        QCOMPARE(capability->busCount, quint8(2));
        QCOMPARE(capability->buses.size(), qsizetype(2));
        QCOMPARE(capability->buses[0].busId, quint8(0));
        QCOMPARE(capability->buses[0].backend, quint8(1));
        QVERIFY(capability->buses[0].controlTxAllowed);
        QCOMPARE(capability->buses[1].busId, quint8(1));
        QVERIFY(!capability->buses[1].controlTxAllowed);
        QCOMPARE(capability->firmwareBuildId, quint32(0xAABBCCDD));
        QCOMPARE(capability->hostTxQueueSize, quint16(32));
    }

    void boardAliveRequiresSerialCapabilityAndFreshHealth() {
        CanMonitorEvidence::BoardConnectionState state;

        state.setSerialOpen(true);
        QVERIFY(!state.boardAlive());
        QVERIFY(state.reason().contains(QStringLiteral("CAPABILITY")));

        state.ingestCapability(capability());
        QVERIFY(!state.boardAlive());
        QVERIFY(state.reason().contains(QStringLiteral("BOARD_HEALTH")));

        state.ingestBoardHealth(health());
        QVERIFY(state.boardAlive());
        QVERIFY(state.controlCapable());

        state.advanceMonotonicTime(2'200'001);
        QVERIFY(!state.boardAlive());
        QVERIFY(!state.controlCapable());
        QVERIFY(state.reason().contains(QStringLiteral("stale")));
    }

    void boardAliveExpiresOnWallClockWhenStreamStops() {
        CanMonitorEvidence::BoardConnectionState state(kTypedTransportVersion, 2'000'000, 2'500);

        state.setSerialOpen(true);
        state.ingestCapability(capability(), 1'000);
        state.ingestBoardHealth(health(), 1'000);
        QVERIFY(state.boardAlive());
        QVERIFY(state.controlCapable());

        state.advanceWallTimeMs(3'400);
        QVERIFY(state.boardAlive());

        state.advanceWallTimeMs(3'501);
        QVERIFY(!state.boardAlive());
        QVERIFY(!state.controlCapable());
        QVERIFY(state.reason().contains(QStringLiteral("stale")));
        QVERIFY(state.snapshot().healthAgeMs >= 2'501);
    }

    void evidenceRuntimeOwnsBoardConnectionState() {
        CanMonitorEvidence::EvidenceRuntime runtime;
        runtime.reset(true);
        QVERIFY(!runtime.boardAlive());
        QVERIFY(runtime.reason().contains(QStringLiteral("CAPABILITY")));

        runtime.ingestCapability(capability(), 10);
        runtime.ingestBoardHealth(health(200, 1), 20);
        QVERIFY(runtime.boardAlive());
        QVERIFY(runtime.controlCapable());
        QCOMPARE(runtime.snapshot().lastHealthWallMs, quint64(20));

        runtime.advanceWallTimeMs(2'800);
        QVERIFY(!runtime.boardAlive());
        QVERIFY(!runtime.controlCapable());
        QVERIFY(runtime.reason().contains(QStringLiteral("stale")));
    }

    void actualCsmArmedAndActiveSafetyStatesStayControlCapable() {
        for (quint8 safetyState : {quint8(1), quint8(2), quint8(3), quint8(4)}) {
            CanMonitorEvidence::BoardConnectionState state;
            state.setSerialOpen(true);
            state.ingestCapability(capability());
            state.ingestBoardHealth(health(200, safetyState));

            QVERIFY2(state.boardAlive(), qPrintable(QStringLiteral("state %1 should be alive").arg(safetyState)));
            QVERIFY2(state.controlCapable(), qPrintable(QStringLiteral("state %1 should be control-capable").arg(safetyState)));
        }

        CanMonitorEvidence::BoardConnectionState timeoutState;
        timeoutState.setSerialOpen(true);
        timeoutState.ingestCapability(capability());
        timeoutState.ingestBoardHealth(health(200, 5));

        QVERIFY(timeoutState.boardAlive());
        QVERIFY(!timeoutState.controlCapable());
        QVERIFY(timeoutState.reason().contains(QStringLiteral("safety")));
    }

    void protocolMismatchBlocksAliveEvenWithHealth() {
        CanMonitorEvidence::BoardConnectionState state;
        state.setSerialOpen(true);
        state.ingestCapability(capability(0x7F));
        state.ingestBoardHealth(health());

        QVERIFY(!state.boardAlive());
        QVERIFY(!state.controlCapable());
        QVERIFY(state.reason().contains(QStringLiteral("protocol")));
    }

    void busRoleResolverDoesNotHardcodeBusNumbers() {
        CanMonitorEvidence::BusRoleResolver resolver;
        resolver.addModelRule(QStringLiteral("drive"), QSet<quint32>{0x510, 0x511, 0x512, 0x513}, true);
        resolver.addModelRule(QStringLiteral("system"), QSet<quint32>{0x321}, false);

        QVERIFY(!resolver.resolve(0).resolved);
        QVERIFY(!resolver.txAllowed(0));

        resolver.observeCanId(0, 0x510);
        auto drive = resolver.resolve(0);
        QVERIFY(drive.resolved);
        QCOMPARE(drive.role, QStringLiteral("drive"));
        QVERIFY(drive.txAllowed);
        QCOMPARE(drive.source, CanMonitorEvidence::BusRoleResolver::Source::ObservedFingerprint);

        resolver.observeCanId(1, 0x321);
        auto system = resolver.resolve(1);
        QVERIFY(system.resolved);
        QCOMPARE(system.role, QStringLiteral("system"));
        QVERIFY(!system.txAllowed);
    }

    void capabilityDescriptorWinsBeforeOperatorFallback() {
        CanMonitorEvidence::BusRoleResolver resolver;
        resolver.addModelRule(QStringLiteral("drive"), QSet<quint32>{0x510}, true);
        resolver.setOperatorOverride(2, QStringLiteral("drive"), true);
        resolver.setCapabilityRole(2, QStringLiteral("system"), false);
        resolver.observeCanId(2, 0x510);

        const auto resolved = resolver.resolve(2);
        QVERIFY(resolved.resolved);
        QCOMPARE(resolved.role, QStringLiteral("system"));
        QVERIFY(!resolved.txAllowed);
        QCOMPARE(resolved.source, CanMonitorEvidence::BusRoleResolver::Source::Capability);
    }
};

QTEST_APPLESS_MAIN(EvidenceRuntimeFoundationTest)

#include "test_evidence_runtime_foundation.moc"
