#pragma once

#include "ControlSlewLimiter.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CanMonitorControl {

class ControlCycleRuntime {
public:
    struct HostFrameIntent {
        QByteArray frame;
        QString summary;
    };

    struct CycleResult {
        QVector<HostFrameIntent> frames;
        QStringList errors;
        bool scheduleGap = false;
        int gapMs = 0;
    };

    int start(int signedCommand,
              int rpm,
              double steeringDeg,
              quint8 motorMode,
              quint8 drivingMode,
              quint8 bus,
              int periodMs,
              int frameGapMs);
    void update(int signedCommand, int rpm, double steeringDeg, quint8 motorMode, quint8 drivingMode, quint8 bus);
    void stop();

    bool enabled() const { return m_enabled; }
    int periodMs() const { return m_periodMs; }
    quint8 bus() const { return m_bus; }

    CycleResult beginCycle();
    CycleResult continuePacedBurst();
    CycleResult burstOnce(int signedCommand,
                          int rpm,
                          double steeringDeg,
                          quint8 motorMode,
                          quint8 drivingMode,
                          quint8 bus,
                          const QString& reason,
                          bool resetSlew = false,
                          bool respectFrameGap = true);

private:
    QVector<HostFrameIntent> maintainSession();
    CycleResult makeBurst(const QString& reason, bool respectFrameGap = true);
    CycleResult drainPendingFrames(bool respectFrameGap = true);
    void clearPending();
    quint32 nextCommandId();

    bool m_enabled = false;
    bool m_sending = false;
    int m_periodMs = 20;
    int m_frameGapMs = 2;
    int m_signedCommand = 0;
    int m_rpm = 0;
    double m_steeringDeg = 0.0;
    ControlSlewLimiter m_slewLimiter;
    quint8 m_motorMode = 1;
    quint8 m_drivingMode = 1;
    quint8 m_bus = 0;
    quint8 m_aliveCounter = 0;
    quint32 m_commandCounter = 0x80000000u;
    qint64 m_lastHeartbeatMs = -1;
    qint64 m_lastLeaseRenewMs = -1;
    QElapsedTimer m_clock;
    QVector<HostFrameIntent> m_pendingFrames;
    int m_pendingIndex = 0;
};

} // namespace CanMonitorControl
