#include "evidence/BoardConnectionState.h"

namespace CanMonitorEvidence {

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
    out.safetyState = m_healthSeen ? m_health.safetyState : 0;
    out.faultFlags = m_healthSeen ? m_health.faultFlags : 0;

    out.boardAlive = out.serialOpen
        && out.capabilitySeen
        && out.healthSeen
        && out.protocolCompatible
        && out.healthFresh
        && m_capability.supportsBoardHealth;

    out.controlCapable = out.boardAlive
        && m_capability.supportsCanTxRaw
        && safetyAllowsControlStandby()
        && m_health.faultFlags == 0;

    if (!out.serialOpen) out.reason = QStringLiteral("serial closed");
    else if (!out.capabilitySeen) out.reason = QStringLiteral("waiting for CAPABILITY");
    else if (!out.protocolCompatible) out.reason = QStringLiteral("protocol mismatch");
    else if (!m_capability.supportsBoardHealth) out.reason = QStringLiteral("BOARD_HEALTH not advertised");
    else if (!out.healthSeen) out.reason = QStringLiteral("waiting for BOARD_HEALTH");
    else if (!out.healthFresh) out.reason = QStringLiteral("BOARD_HEALTH stale");
    else if (!m_capability.supportsCanTxRaw) out.reason = QStringLiteral("CAN_TX_RAW audit not advertised");
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
