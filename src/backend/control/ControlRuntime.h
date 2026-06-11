#pragma once

#include "../ControlCommandEncoder.h"

#include <QString>
#include <QVariantList>
#include <QtGlobal>

namespace CanMonitorControl {

struct ControlTarget {
    int bus = kDefaultControlBus;
    int rpm = 0;
    double steeringDeg = 0.0;
};

struct ControlCommandIntent {
    int signedCommand = 0;
    int rpm = 0;
    double steeringDeg = 0.0;
    quint8 motorMode = 1;
    quint8 drivingMode = 1;
};

class ControlRuntime {
public:
    bool armed() const { return m_armed; }
    bool testRunning() const { return m_testRunning; }
    ControlTarget target() const { return m_target; }
    ControlCommandIntent currentIntent() const { return m_currentIntent; }
    QString statusText() const { return m_statusText; }
    QString lastCommandSummary() const { return m_lastCommandSummary; }
    bool targetBusManualOverride() const { return m_targetBusManualOverride; }
    qint64 lastHeartbeatWallMs() const { return m_lastHeartbeatWallMs; }
    qint64 lastLeaseRenewWallMs() const { return m_lastLeaseRenewWallMs; }
    qint64 lastBurstWallMs() const { return m_lastBurstWallMs; }

    void setArmed(bool armed);
    void setTestRunning(bool running);
    void setTargetBus(int bus, bool manualOverride);
    void setTargetRpm(int rpm);
    void setTargetSteeringDeg(double deg);
    void setCurrentIntent(int signedCommand, int rpm, double steeringDeg, quint8 motorMode, quint8 drivingMode);
    void setStatusText(const QString& status);
    void setLastCommandSummary(const QString& summary);
    void setLastHeartbeatWallMs(qint64 wallMs);
    void setLastLeaseRenewWallMs(qint64 wallMs);
    void setLastBurstWallMs(qint64 wallMs);
    void clearBurstWallMs();
    void resetPatternState();
    quint32 nextCommandId();
    quint8 nextAliveCounter();
    QString statusSummary(const QString& transportMode,
                          bool connected,
                          bool evidenceReady,
                          const QString& evidenceBlockReason) const;
    QString actionVerdict(const QString& transportMode,
                          bool connected,
                          bool boardAlive,
                          bool controlCapable,
                          bool targetBusAllowed,
                          bool actualTxConfirmed,
                          bool faultActive,
                          const QString& blockReason) const;
    QVariantList operatorChecklist(const QString& transportMode,
                                   bool connected,
                                   bool boardAlive,
                                   bool controlCapable,
                                   bool targetBusAllowed,
                                   bool actualTxConfirmed,
                                   bool faultActive,
                                   const QString& blockReason) const;

private:
    bool m_armed = false;
    bool m_testRunning = false;
    ControlTarget m_target;
    bool m_targetBusManualOverride = false;
    ControlCommandIntent m_currentIntent;
    quint8 m_aliveCounter = 0;
    quint32 m_commandCounter = 0;
    qint64 m_lastHeartbeatWallMs = 0;
    qint64 m_lastLeaseRenewWallMs = 0;
    qint64 m_lastBurstWallMs = 0;
    QString m_statusText = QStringLiteral("Control locked: typed stream + connect + arm required");
    QString m_lastCommandSummary = QStringLiteral("-");
};

} // namespace CanMonitorControl
