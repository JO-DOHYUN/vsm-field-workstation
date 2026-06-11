#include "control/ControlCycleRuntime.h"

#include "ControlCommandEncoder.h"

#include <algorithm>

namespace {
constexpr int kControlHeartbeatPeriodMs = 50;
constexpr int kControlLeaseRenewPeriodMs = 250;
constexpr quint16 kControlLeaseMs = 2000;
}

namespace CanMonitorControl {

int ControlCycleRuntime::start(int signedCommand,
                               int rpm,
                               double steeringDeg,
                               quint8 motorMode,
                               quint8 drivingMode,
                               quint8 bus,
                               int periodMs,
                               int frameGapMs) {
    update(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus);
    m_slewLimiter.reset();
    m_slewLimiter.setTarget({m_signedCommand, m_rpm, m_steeringDeg});
    m_periodMs = std::clamp(periodMs, 5, 1000);
    m_frameGapMs = std::clamp(frameGapMs, 0, 20);
    m_enabled = true;
    m_clock.restart();
    m_lastHeartbeatMs = -1;
    m_lastLeaseRenewMs = -1;
    clearPending();
    return m_periodMs;
}

void ControlCycleRuntime::update(int signedCommand,
                                 int rpm,
                                 double steeringDeg,
                                 quint8 motorMode,
                                 quint8 drivingMode,
                                 quint8 bus) {
    m_signedCommand = std::clamp(signedCommand, -10000, 10000);
    m_rpm = std::clamp(rpm, 0, 10000);
    m_steeringDeg = std::clamp(steeringDeg, -90.0, 90.0);
    m_motorMode = motorMode == 2 ? 2 : 1;
    m_drivingMode = drivingMode;
    m_bus = bus;
    m_slewLimiter.setTarget({m_signedCommand, m_rpm, m_steeringDeg});
}

void ControlCycleRuntime::stop() {
    m_enabled = false;
    m_signedCommand = 0;
    m_rpm = 0;
    m_steeringDeg = 0.0;
    m_slewLimiter.reset();
    clearPending();
}

ControlCycleRuntime::CycleResult ControlCycleRuntime::beginCycle() {
    CycleResult result;
    if (!m_enabled) return result;

    result.frames += maintainSession();
    if (m_sending) {
        CycleResult pending = drainPendingFrames();
        result.frames += pending.frames;
        result.errors += pending.errors;
        result.scheduleGap = pending.scheduleGap;
        result.gapMs = pending.gapMs;
        return result;
    }

    CycleResult burst = makeBurst(QStringLiteral("worker control cycle"));
    result.frames += burst.frames;
    result.errors += burst.errors;
    result.scheduleGap = burst.scheduleGap;
    result.gapMs = burst.gapMs;
    return result;
}

ControlCycleRuntime::CycleResult ControlCycleRuntime::continuePacedBurst() {
    return drainPendingFrames();
}

ControlCycleRuntime::CycleResult ControlCycleRuntime::burstOnce(int signedCommand,
                                                                int rpm,
                                                                double steeringDeg,
                                                                quint8 motorMode,
                                                                quint8 drivingMode,
                                                                quint8 bus,
                                                                const QString& reason,
                                                                bool resetSlew,
                                                                bool respectFrameGap) {
    update(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus);
    if (resetSlew) {
        m_slewLimiter.reset({m_signedCommand, m_rpm, m_steeringDeg});
    }
    return makeBurst(reason.isEmpty() ? QStringLiteral("worker one-shot") : reason, respectFrameGap);
}

QVector<ControlCycleRuntime::HostFrameIntent> ControlCycleRuntime::maintainSession() {
    QVector<HostFrameIntent> frames;
    if (!m_clock.isValid()) m_clock.restart();
    const qint64 nowMs = m_clock.elapsed();

    if (m_lastHeartbeatMs < 0 || nowMs - m_lastHeartbeatMs >= kControlHeartbeatPeriodMs) {
        const quint32 commandId = nextCommandId();
        const quint32 hostMonoMs = quint32(nowMs & 0xFFFFFFFFLL);
        frames.push_back({
            ControlCommandEncoder::buildHostHeartbeat(commandId, hostMonoMs),
            QStringLiteral("#%1 worker HOST_HEARTBEAT").arg(commandId),
        });
        m_lastHeartbeatMs = nowMs;
    }

    if (m_lastLeaseRenewMs < 0 || nowMs - m_lastLeaseRenewMs >= kControlLeaseRenewPeriodMs) {
        const quint32 commandId = nextCommandId();
        frames.push_back({
            ControlCommandEncoder::buildHostControlSession(commandId,
                                                           kControlSessionRenewLease,
                                                           kControlSessionAnyBus,
                                                           kControlLeaseMs),
            QStringLiteral("#%1 worker HOST_CONTROL_SESSION RENEW_LEASE").arg(commandId),
        });
        m_lastLeaseRenewMs = nowMs;
    }

    return frames;
}

ControlCycleRuntime::CycleResult ControlCycleRuntime::makeBurst(const QString& reason, bool respectFrameGap) {
    const auto commandState = m_slewLimiter.step();
    const auto frames = ControlCommandEncoder::makeControlBurst(commandState.signedCommand,
                                                                commandState.rpm,
                                                                commandState.steeringDeg,
                                                                m_motorMode,
                                                                m_drivingMode,
                                                                m_aliveCounter++,
                                                                m_bus);

    clearPending();
    m_pendingFrames.reserve(frames.size());
    for (const auto& frame : frames) {
        const quint32 commandId = nextCommandId();
        m_pendingFrames.push_back({
            ControlCommandEncoder::buildHostCanTxRequest(commandId, frame),
            QStringLiteral("#%1 %2 %3")
                .arg(commandId)
                .arg(reason)
                .arg(ControlCommandEncoder::frameSummary(frame)),
        });
    }
    m_pendingIndex = 0;
    m_sending = true;
    return drainPendingFrames(respectFrameGap);
}

ControlCycleRuntime::CycleResult ControlCycleRuntime::drainPendingFrames(bool respectFrameGap) {
    CycleResult result;
    while (m_pendingIndex < m_pendingFrames.size()) {
        result.frames.push_back(m_pendingFrames.at(m_pendingIndex++));
        if (m_pendingIndex >= m_pendingFrames.size()) {
            clearPending();
            break;
        }
        if (respectFrameGap && m_frameGapMs > 0) {
            result.scheduleGap = true;
            result.gapMs = m_frameGapMs;
            break;
        }
    }
    if (m_pendingFrames.isEmpty()) m_sending = false;
    return result;
}

void ControlCycleRuntime::clearPending() {
    m_pendingFrames.clear();
    m_pendingIndex = 0;
    m_sending = false;
}

quint32 ControlCycleRuntime::nextCommandId() {
    if (m_commandCounter < 0x80000000u) {
        m_commandCounter = 0x80000000u;
    }
    return ++m_commandCounter;
}

} // namespace CanMonitorControl
