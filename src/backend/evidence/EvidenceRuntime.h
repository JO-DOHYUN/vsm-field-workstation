#pragma once

#include "evidence/BoardConnectionState.h"

namespace CanMonitorEvidence {

class EvidenceRuntime {
public:
    using Snapshot = BoardConnectionState::Snapshot;

    void reset(bool serialOpen = false);
    void setSerialOpen(bool open);
    void advanceWallTimeMs(quint64 wallMs);
    void ingestCapability(const TypedCapabilityRecord& capability, quint64 wallMs = 0);
    void ingestBoardHealth(const TypedBoardHealthRecord& health, quint64 wallMs = 0);

    bool boardAlive() const;
    bool controlCapable() const;
    QString reason() const;
    Snapshot snapshot() const;

private:
    BoardConnectionState m_boardConnection;
};

} // namespace CanMonitorEvidence
