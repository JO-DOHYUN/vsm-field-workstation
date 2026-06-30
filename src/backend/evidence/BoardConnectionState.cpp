#include "evidence/BoardConnectionState.h"

namespace CanMonitorEvidence {

namespace {

constexpr quint8 kCsmFirmwareProfilePassiveProduct = 1;
constexpr quint8 kCsmVehicleImpactVerifiedPassive = 3;
constexpr quint8 kCsmBusModeListenOnly = 1;
constexpr quint8 kCsmBusModeHardwareSilent = 2;
constexpr quint8 kCsmBusModeNormal = 3;

bool hasBoardTxOrControl(const TypedCapabilityRecord& capability) {
    for (const TypedCapabilityBusDescriptor& bus : capability.buses) {
        if (bus.txSupported || bus.controlTxAllowed) {
            return true;
        }
    }
    return false;
}

bool hasHostActivePath(const TypedCapabilityRecord& capability) {
    if (capability.hasPassivePolicy) {
        return capability.hostCommandRx ||
               capability.controlPath ||
               capability.supportedDownlinkRecords != 0 ||
               capability.hostTxQueueSize != 0 ||
               hasBoardTxOrControl(capability);
    }
    return capability.supportsCanTxRaw ||
           capability.supportedDownlinkRecords != 0 ||
           capability.hostTxQueueSize != 0 ||
           hasBoardTxOrControl(capability);
}

bool hasControlPath(const TypedCapabilityRecord& capability) {
    if (!capability.hasPassivePolicy && capability.supportsCanTxRaw) {
        return true;
    }
    return capability.controlPath ||
           capability.supportedDownlinkRecords != 0 ||
           capability.hostTxQueueSize != 0 ||
           hasBoardTxOrControl(capability);
}

bool hasVehicleImpactCapableRxBus(const TypedCapabilityRecord& capability) {
    if (!capability.hasPassivePolicy) {
        return false;
    }
    for (qsizetype index = 0; index < capability.buses.size(); ++index) {
        const TypedCapabilityBusDescriptor& bus = capability.buses.at(index);
        if (!bus.rxSupported) {
            continue;
        }
        const int policyIndex = bus.busId < 2 ? int(bus.busId) : int(index);
        const bool hasPolicySlot = policyIndex >= 0 && policyIndex < 2;
        const quint8 busMode = hasPolicySlot ? capability.busMode[policyIndex] : quint8(0);
        const bool modeIsPassive = busMode == kCsmBusModeListenOnly ||
                                   busMode == kCsmBusModeHardwareSilent;
        const bool canAffectBus = !hasPolicySlot ||
                                  capability.busAckCapable[policyIndex] ||
                                  capability.busErrorFrameCapable[policyIndex] ||
                                  busMode == kCsmBusModeNormal ||
                                  !modeIsPassive;
        if (canAffectBus) {
            return true;
        }
    }
    return false;
}

bool busPolicyIsPassive(const TypedCapabilityRecord& capability, int busId) {
    if (!capability.hasPassivePolicy || busId < 0 || busId >= 2) {
        return false;
    }
    const quint8 busMode = capability.busMode[busId];
    return (busMode == kCsmBusModeListenOnly || busMode == kCsmBusModeHardwareSilent) &&
           !capability.busAckCapable[busId] &&
           !capability.busErrorFrameCapable[busId];
}

bool hasRequiredTwoBusRxOnlyProductProfile(const TypedCapabilityRecord& capability) {
    bool bus0Rx = false;
    bool bus1Rx = false;
    bool bus0RxOnly = false;
    bool bus1RxOnly = false;
    for (const TypedCapabilityBusDescriptor& bus : capability.buses) {
        if (bus.busId != 0 && bus.busId != 1) {
            continue;
        }
        const bool rxOnly = bus.rxSupported && !bus.txSupported && !bus.controlTxAllowed;
        if (bus.busId == 0) {
            bus0Rx = bus.rxSupported;
            bus0RxOnly = rxOnly;
        } else if (bus.busId == 1) {
            bus1Rx = bus.rxSupported;
            bus1RxOnly = rxOnly;
        }
    }
    return capability.busCount >= 2 &&
           bus0Rx &&
           bus1Rx &&
           bus0RxOnly &&
           bus1RxOnly &&
           busPolicyIsPassive(capability, 0) &&
           busPolicyIsPassive(capability, 1);
}

bool hardwareEvidenceClaimIsComplete(const TypedCapabilityRecord& capability) {
    if (!capability.hasPassiveHardwareEvidenceClaims) {
        return false;
    }
    for (int bus = 0; bus < 2; ++bus) {
        if (!capability.hardwareSilentStrapped[bus] ||
            !capability.galvanicIsolated[bus] ||
            !capability.powerOffPassive[bus] ||
            !capability.resetSafe[bus] ||
            !capability.txdGated[bus] ||
            capability.normalEnablePathPopulated[bus]) {
            return false;
        }
    }
    return capability.hardwareSafetyCaseId != 0 &&
           capability.benchVerificationId != 0 &&
           capability.fieldSkuId != 0 &&
           capability.externalAnalyzerArtifactId != 0 &&
           capability.hotplugPassCount > 0;
}

} // namespace

BoardConnectionState::BoardConnectionState(quint8 requiredProtocolVersion,
                                           quint64 healthFreshWindowUs,
                                           quint64 healthFreshWindowMs)
    : m_requiredProtocolVersion(requiredProtocolVersion),
      m_healthFreshWindowUs(healthFreshWindowUs),
      m_healthFreshWindowMs(healthFreshWindowMs) {}

void BoardConnectionState::reset() {
    m_nowMonoUs = 0;
    m_nowWallMs = 0;
    m_lastCapabilityWallMs = 0;
    m_lastHealthWallMs = 0;
    m_serialOpen = false;
    m_capabilitySeen = false;
    m_healthSeen = false;
    m_externalPassiveEvidence = ExternalPassiveEvidence{};
    m_capability = TypedCapabilityRecord{};
    m_health = TypedBoardHealthRecord{};
}

void BoardConnectionState::setSerialOpen(bool open) {
    if (!open) {
        reset();
        return;
    }
    m_serialOpen = true;
}

void BoardConnectionState::ingestCapability(const TypedCapabilityRecord& capability, quint64 wallMs) {
    m_capability = capability;
    m_capabilitySeen = true;
    if (capability.monoUs > m_nowMonoUs) m_nowMonoUs = capability.monoUs;
    if (wallMs > 0) {
        if (wallMs > m_nowWallMs) m_nowWallMs = wallMs;
        m_lastCapabilityWallMs = wallMs;
    } else if (m_nowWallMs > 0) {
        m_lastCapabilityWallMs = m_nowWallMs;
    }
}

void BoardConnectionState::ingestBoardHealth(const TypedBoardHealthRecord& health, quint64 wallMs) {
    m_health = health;
    m_healthSeen = true;
    if (health.monoUs > m_nowMonoUs) m_nowMonoUs = health.monoUs;
    if (wallMs > 0) {
        if (wallMs > m_nowWallMs) m_nowWallMs = wallMs;
        m_lastHealthWallMs = wallMs;
    } else if (m_nowWallMs > 0) {
        m_lastHealthWallMs = m_nowWallMs;
    }
}

void BoardConnectionState::setExternalPassiveEvidence(const ExternalPassiveEvidence& evidence) {
    m_externalPassiveEvidence = evidence;
}

void BoardConnectionState::advanceMonotonicTime(quint64 monoUs) {
    if (monoUs > m_nowMonoUs) m_nowMonoUs = monoUs;
}

void BoardConnectionState::advanceWallTimeMs(quint64 wallMs) {
    if (wallMs > m_nowWallMs) m_nowWallMs = wallMs;
}

BoardConnectionState::Snapshot BoardConnectionState::snapshot() const {
    return computeSnapshot();
}

bool BoardConnectionState::boardAlive() const {
    return computeSnapshot().boardAlive;
}

bool BoardConnectionState::controlCapable() const {
    return computeSnapshot().controlCapable;
}

QString BoardConnectionState::reason() const {
    return computeSnapshot().reason;
}

BoardConnectionState::Snapshot BoardConnectionState::computeSnapshot() const {
    Snapshot out;
    out.serialOpen = m_serialOpen;
    out.capabilitySeen = m_capabilitySeen;
    out.healthSeen = m_healthSeen;
    out.protocolCompatible = m_capabilitySeen && m_capability.protocolVersion == m_requiredProtocolVersion;
    out.healthFresh = healthIsFresh();
    out.lastCapabilityMonoUs = m_capabilitySeen ? m_capability.monoUs : 0;
    out.lastHealthMonoUs = m_healthSeen ? m_health.monoUs : 0;
    out.lastHealthWallMs = m_healthSeen ? m_lastHealthWallMs : 0;
    out.healthAgeMs = (m_healthSeen && m_lastHealthWallMs > 0 && m_nowWallMs >= m_lastHealthWallMs)
        ? (m_nowWallMs - m_lastHealthWallMs)
        : 0;
    out.profileMajor = m_capabilitySeen ? m_capability.profileMajor : 0;
    out.profileMinor = m_capabilitySeen ? m_capability.profileMinor : 0;
    out.csmPassivePolicySeen = m_capabilitySeen && m_capability.hasPassivePolicy;
    out.usbCdcDtrSessionRequired = m_capabilitySeen && m_capability.usbCdcDtrSessionRequired;
    out.usbCdcDtrSessionOnly = m_capabilitySeen && m_capability.usbCdcDtrSessionOnly;
    out.dtrResetSensitive = m_capabilitySeen && m_capability.dtrResetSensitive;
    out.passiveAcceptanceAllowed = m_capabilitySeen && m_capability.passiveAcceptanceAllowed;
    out.firmwareProfile = m_capabilitySeen ? m_capability.firmwareProfile : 0;
    out.vehicleImpactState = m_capabilitySeen ? m_capability.vehicleImpactState : 0;
    out.safetyState = m_healthSeen ? m_health.safetyState : 0;
    out.faultFlags = m_healthSeen ? m_health.faultFlags : 0;
    out.hardwareSafetyCaseId = m_capabilitySeen ? m_capability.hardwareSafetyCaseId : 0;
    out.benchVerificationId = m_capabilitySeen ? m_capability.benchVerificationId : 0;
    out.fieldSkuId = m_capabilitySeen ? m_capability.fieldSkuId : 0;
    out.externalAnalyzerArtifactId = m_capabilitySeen ? m_capability.externalAnalyzerArtifactId : 0;
    out.hotplugPassCount = m_capabilitySeen ? m_capability.hotplugPassCount : 0;
    out.hostSessionEpoch = m_capabilitySeen ? m_capability.hostSessionEpoch : 0;
    out.transportEpoch = m_capabilitySeen ? m_capability.transportEpoch : 0;
    out.usbAttachQuarantineTotal = m_capabilitySeen ? m_capability.usbAttachQuarantineTotal : 0;
    out.hostAbsentGapTotal = m_capabilitySeen ? m_capability.hostAbsentGapTotal : 0;
    out.preSessionPayloadReplayTotal = m_capabilitySeen ? m_capability.preSessionPayloadReplayTotal : 0;

    const bool activePath = m_capabilitySeen && hasHostActivePath(m_capability);
    const bool vehicleImpactPath = m_capabilitySeen && hasVehicleImpactCapableRxBus(m_capability);
    const bool twoBusRequirement = m_capabilitySeen && hasRequiredTwoBusRxOnlyProductProfile(m_capability);
    const bool passivePolicyProfile = m_capabilitySeen &&
        m_capability.hasPassivePolicy &&
        m_capability.firmwareProfile == kCsmFirmwareProfilePassiveProduct &&
        !m_capability.hostCommandRx &&
        !m_capability.controlPath;
    const bool healthRuntimePassive = !m_healthSeen ||
        (m_health.passiveReadbackViolationTotal == 0 && m_health.txreqViolationTotal == 0);
    out.csmActiveCapable = activePath;
    out.csmVehicleImpactPossible = vehicleImpactPath;
    out.twoBusProductRequirementSatisfied = twoBusRequirement;
    out.configuredPassive = passivePolicyProfile &&
        twoBusRequirement &&
        !out.csmActiveCapable &&
        !out.csmVehicleImpactPossible &&
        m_capability.supportsCanRxRaw &&
        m_capability.supportsBoardHealth;
    out.runtimePassive = out.configuredPassive && healthRuntimePassive;
    out.hardwareEvidenceClaimed = m_capabilitySeen && m_capability.hasPassiveHardwareEvidenceClaims;
    out.hardwareEvidenceCompleteClaim = m_capabilitySeen && hardwareEvidenceClaimIsComplete(m_capability);
    out.externalPassiveEvidenceVerified = m_externalPassiveEvidence.isComplete() &&
        (!m_capabilitySeen || m_externalPassiveEvidence.artifactId == m_capability.externalAnalyzerArtifactId) &&
        (!m_capabilitySeen || m_externalPassiveEvidence.hotplugPassCount >= m_capability.hotplugPassCount);
    out.verifiedPassive = out.runtimePassive &&
        out.hardwareEvidenceCompleteClaim &&
        out.externalPassiveEvidenceVerified &&
        m_capability.vehicleImpactState == kCsmVehicleImpactVerifiedPassive &&
        m_capability.passiveAcceptanceAllowed;
    out.csmPassiveCapabilityCandidate = m_capabilitySeen &&
        out.configuredPassive;
    if (!m_capabilitySeen) {
        out.profileMatchResult = QStringLiteral("blocked_unknown");
    } else if (out.csmActiveCapable) {
        out.profileMatchResult = QStringLiteral("blocked_active_csm");
    } else if (out.csmVehicleImpactPossible) {
        out.profileMatchResult = QStringLiteral("blocked_vehicle_impact_possible");
    } else if (!out.twoBusProductRequirementSatisfied) {
        out.profileMatchResult = QStringLiteral("blocked_incomplete_2bus_passive_capability");
    } else if (out.verifiedPassive) {
        out.profileMatchResult = QStringLiteral("verified_passive");
    } else if (out.configuredPassive && !out.runtimePassive) {
        out.profileMatchResult = QStringLiteral("configured_passive_runtime_violation");
    } else if (out.configuredPassive && !out.hardwareEvidenceCompleteClaim) {
        out.profileMatchResult = QStringLiteral("configured_passive_hardware_claim_incomplete");
    } else if (out.configuredPassive && !out.externalPassiveEvidenceVerified) {
        out.profileMatchResult = QStringLiteral("configured_passive_external_evidence_unverified");
    } else if (out.csmPassiveCapabilityCandidate) {
        out.profileMatchResult = QStringLiteral("csm_passive_candidate_hardware_unverified");
    } else {
        out.profileMatchResult = QStringLiteral("blocked_incomplete_csm_capability");
    }

    out.boardAlive = out.serialOpen
        && out.capabilitySeen
        && out.healthSeen
        && out.protocolCompatible
        && out.healthFresh
        && m_capability.supportsBoardHealth;

    out.controlCapable = out.boardAlive
        && hasControlPath(m_capability)
        && safetyAllowsControlStandby()
        && m_health.faultFlags == 0;

    if (!out.serialOpen) out.reason = QStringLiteral("serial closed");
    else if (!out.capabilitySeen) out.reason = QStringLiteral("waiting for CAPABILITY");
    else if (!out.protocolCompatible) out.reason = QStringLiteral("protocol mismatch");
    else if (!m_capability.supportsBoardHealth) out.reason = QStringLiteral("BOARD_HEALTH not advertised");
    else if (!out.healthSeen) out.reason = QStringLiteral("waiting for BOARD_HEALTH");
    else if (!out.healthFresh) out.reason = QStringLiteral("BOARD_HEALTH stale");
    else if (!hasControlPath(m_capability)) out.reason = QStringLiteral("monitor-only passive CSM");
    else if (!safetyAllowsControlStandby()) out.reason = QStringLiteral("board safety state blocks control");
    else if (m_health.faultFlags != 0) out.reason = QStringLiteral("board fault flags active");
    else out.reason = QStringLiteral("board alive");

    return out;
}

bool BoardConnectionState::healthIsFresh() const {
    if (!m_healthSeen) return false;
    bool monoFresh = true;
    if (m_nowMonoUs > m_health.monoUs) {
        monoFresh = (m_nowMonoUs - m_health.monoUs) <= m_healthFreshWindowUs;
    }
    if (!monoFresh) return false;

    if (m_lastHealthWallMs > 0 && m_nowWallMs > m_lastHealthWallMs) {
        return (m_nowWallMs - m_lastHealthWallMs) <= m_healthFreshWindowMs;
    }
    return true;
}

bool BoardConnectionState::safetyAllowsControlStandby() const {
    // Actual CSM safety states: 1 MonitorOnly, 2 Ready, 3 Armed, 4 ControlActive.
    // States 5+ are timeout/fault/estop and must block host control.
    return m_health.safetyState >= 1 && m_health.safetyState <= 4;
}

} // namespace CanMonitorEvidence
