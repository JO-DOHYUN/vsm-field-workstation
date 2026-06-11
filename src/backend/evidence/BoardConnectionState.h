#pragma once

#include "TypedRecords.h"

#include <QString>
#include <QtGlobal>

namespace CanMonitorEvidence {

class BoardConnectionState {
public:
    struct Snapshot {
        bool serialOpen = false;
        bool capabilitySeen = false;
        bool healthSeen = false;
        bool protocolCompatible = false;
        bool healthFresh = false;
        bool boardAlive = false;
        bool controlCapable = false;
        quint8 profileMajor = 0;
        quint8 profileMinor = 0;
        quint8 safetyState = 0;
        quint32 faultFlags = 0;
        quint64 lastCapabilityMonoUs = 0;
        quint64 lastHealthMonoUs = 0;
        quint64 lastHealthWallMs = 0;
        quint64 healthAgeMs = 0;
        QString reason;
    };

    explicit BoardConnectionState(quint8 requiredProtocolVersion = kTypedTransportVersion,
                                  quint64 healthFreshWindowUs = 2'000'000,
                                  quint64 healthFreshWindowMs = 2'500);

    void reset();
    void setSerialOpen(bool open);
    void ingestCapability(const TypedCapabilityRecord& capability, quint64 wallMs = 0);
    void ingestBoardHealth(const TypedBoardHealthRecord& health, quint64 wallMs = 0);
    void advanceMonotonicTime(quint64 monoUs);
    void advanceWallTimeMs(quint64 wallMs);

    Snapshot snapshot() const;
    bool boardAlive() const;
    bool controlCapable() const;
    QString reason() const;

private:
    Snapshot computeSnapshot() const;
    bool healthIsFresh() const;
    bool safetyAllowsControlStandby() const;

    quint8 m_requiredProtocolVersion = kTypedTransportVersion;
    quint64 m_healthFreshWindowUs = 2'000'000;
    quint64 m_healthFreshWindowMs = 2'500;
    quint64 m_nowMonoUs = 0;
    quint64 m_nowWallMs = 0;
    quint64 m_lastCapabilityWallMs = 0;
    quint64 m_lastHealthWallMs = 0;
    bool m_serialOpen = false;
    bool m_capabilitySeen = false;
    bool m_healthSeen = false;
    TypedCapabilityRecord m_capability;
    TypedBoardHealthRecord m_health;
};

} // namespace CanMonitorEvidence
