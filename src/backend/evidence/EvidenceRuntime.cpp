#include "evidence/EvidenceRuntime.h"

namespace CanMonitorEvidence {

void EvidenceRuntime::reset(bool serialOpen) {
    m_boardConnection.reset();
    m_boardConnection.setSerialOpen(serialOpen);
}

void EvidenceRuntime::setSerialOpen(bool open) {
    m_boardConnection.setSerialOpen(open);
}

void EvidenceRuntime::advanceWallTimeMs(quint64 wallMs) {
    m_boardConnection.advanceWallTimeMs(wallMs);
}

void EvidenceRuntime::ingestCapability(const TypedCapabilityRecord& capability, quint64 wallMs) {
    m_boardConnection.ingestCapability(capability, wallMs);
}

void EvidenceRuntime::ingestBoardHealth(const TypedBoardHealthRecord& health, quint64 wallMs) {
    m_boardConnection.ingestBoardHealth(health, wallMs);
}

bool EvidenceRuntime::boardAlive() const {
    return m_boardConnection.boardAlive();
}

bool EvidenceRuntime::controlCapable() const {
    return m_boardConnection.controlCapable();
}

QString EvidenceRuntime::reason() const {
    return m_boardConnection.reason();
}

EvidenceRuntime::Snapshot EvidenceRuntime::snapshot() const {
    return m_boardConnection.snapshot();
}

} // namespace CanMonitorEvidence
