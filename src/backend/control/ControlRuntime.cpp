#include "ControlRuntime.h"

#include <QStringList>
#include <QVariantMap>
#include <algorithm>

namespace CanMonitorControl {

void ControlRuntime::setArmed(bool armed) {
    m_armed = armed;
}

void ControlRuntime::setTestRunning(bool running) {
    m_testRunning = running;
}

void ControlRuntime::setTargetBus(int bus, bool manualOverride) {
    m_target.bus = std::clamp(bus, 0, 255);
    if (manualOverride) m_targetBusManualOverride = true;
}

void ControlRuntime::setTargetRpm(int rpm) {
    m_target.rpm = std::clamp(rpm, 0, 10000);
}

void ControlRuntime::setTargetSteeringDeg(double deg) {
    m_target.steeringDeg = std::clamp(deg, -90.0, 90.0);
}

void ControlRuntime::setCurrentIntent(int signedCommand, int rpm, double steeringDeg, quint8 motorMode, quint8 drivingMode) {
    m_currentIntent.signedCommand = std::clamp(signedCommand, -10000, 10000);
    m_currentIntent.rpm = std::clamp(rpm, 0, 10000);
    m_currentIntent.steeringDeg = std::clamp(steeringDeg, -90.0, 90.0);
    m_currentIntent.motorMode = motorMode == 2 ? 2 : 1;
    m_currentIntent.drivingMode = drivingMode;
}

void ControlRuntime::setStatusText(const QString& status) {
    if (!status.isEmpty()) m_statusText = status;
}

void ControlRuntime::setLastCommandSummary(const QString& summary) {
    m_lastCommandSummary = summary.isEmpty() ? QStringLiteral("-") : summary;
}

void ControlRuntime::setLastHeartbeatWallMs(qint64 wallMs) {
    m_lastHeartbeatWallMs = wallMs;
}

void ControlRuntime::setLastLeaseRenewWallMs(qint64 wallMs) {
    m_lastLeaseRenewWallMs = wallMs;
}

void ControlRuntime::setLastBurstWallMs(qint64 wallMs) {
    m_lastBurstWallMs = wallMs;
}

void ControlRuntime::clearBurstWallMs() {
    m_lastBurstWallMs = 0;
}

void ControlRuntime::resetPatternState() {
    m_testRunning = false;
    clearBurstWallMs();
}

quint32 ControlRuntime::nextCommandId() {
    return ++m_commandCounter;
}

quint8 ControlRuntime::nextAliveCounter() {
    return m_aliveCounter++;
}

QString ControlRuntime::statusSummary(const QString& transportMode,
                                      bool connected,
                                      bool evidenceReady,
                                      const QString& evidenceBlockReason) const {
    QStringList parts;
    parts << (m_armed ? QStringLiteral("제어 ARM") : QStringLiteral("대기"));
    parts << (m_testRunning ? QStringLiteral("테스트 실행 중") : QStringLiteral("수동"));
    parts << QStringLiteral("BUS %1").arg(m_target.bus);
    parts << QStringLiteral("목표 rpm %1").arg(m_target.rpm);
    parts << QStringLiteral("목표 조향 %1 deg").arg(QString::number(m_target.steeringDeg, 'f', 1));
    parts << QStringLiteral("출력 제한 조향/rpm slew 적용");
    if (transportMode != QStringLiteral("typed")) parts << QStringLiteral("typed 모드 필요");
    if (!connected) parts << QStringLiteral("미연결");
    if (transportMode == QStringLiteral("typed") && connected && !evidenceReady) {
        parts << QStringLiteral("보드 gate: %1").arg(evidenceBlockReason);
    }
    if (!m_statusText.isEmpty()) parts << m_statusText;
    return parts.join(QStringLiteral(" | "));
}

QString ControlRuntime::actionVerdict(const QString& transportMode,
                                      bool connected,
                                      bool boardAlive,
                                      bool controlCapable,
                                      bool targetBusAllowed,
                                      bool actualTxConfirmed,
                                      bool faultActive,
                                      const QString& blockReason) const {
    if (transportMode != QStringLiteral("typed")) return QStringLiteral("차단: typed evidence stream 필요");
    if (!connected) return QStringLiteral("차단: COM 연결 없음");
    if (!boardAlive) return QStringLiteral("차단: CAPABILITY/BOARD_HEALTH 기준 board alive 아님");
    if (!controlCapable) return QStringLiteral("차단: %1").arg(blockReason.isEmpty() ? QStringLiteral("board control gate") : blockReason);
    if (!targetBusAllowed) return QStringLiteral("차단: target BUS %1 TX 권한 없음").arg(m_target.bus);
    if (faultActive) return QStringLiteral("차단: control fault/block event 활성");
    if (!m_armed) return QStringLiteral("대기: ARM 후 motion command 가능");
    if (!actualTxConfirmed) return QStringLiteral("송신 대기: 성공 표시는 matching CAN_TX_RAW 필요");
    return QStringLiteral("송신 확인: matching CAN_TX_RAW audit 기준");
}

QVariantList ControlRuntime::operatorChecklist(const QString& transportMode,
                                               bool connected,
                                               bool boardAlive,
                                               bool controlCapable,
                                               bool targetBusAllowed,
                                               bool actualTxConfirmed,
                                               bool faultActive,
                                               const QString& blockReason) const {
    auto row = [](const QString& key,
                  const QString& title,
                  const QString& level,
                  const QString& state,
                  const QString& detail,
                  bool ok,
                  bool blocking) {
        QVariantMap item;
        item.insert(QStringLiteral("key"), key);
        item.insert(QStringLiteral("title"), title);
        item.insert(QStringLiteral("level"), level);
        item.insert(QStringLiteral("state"), state);
        item.insert(QStringLiteral("detail"), detail);
        item.insert(QStringLiteral("ok"), ok);
        item.insert(QStringLiteral("blocking"), blocking);
        return item;
    };

    const bool typed = transportMode == QStringLiteral("typed");
    QVariantList rows;
    rows << row(QStringLiteral("mode"),
                QStringLiteral("Typed stream"),
                typed ? QStringLiteral("ok") : QStringLiteral("error"),
                typed ? QStringLiteral("OK") : QStringLiteral("BLOCK"),
                typed ? QStringLiteral("production evidence path") : QStringLiteral("legacy/replay path cannot arm control"),
                typed,
                !typed);
    rows << row(QStringLiteral("serial"),
                QStringLiteral("COM"),
                connected ? QStringLiteral("ok") : QStringLiteral("error"),
                connected ? QStringLiteral("OPEN") : QStringLiteral("CLOSED"),
                connected ? QStringLiteral("serial open only; board alive still separate") : QStringLiteral("connect typed CSM first"),
                connected,
                !connected);
    rows << row(QStringLiteral("board"),
                QStringLiteral("Board gate"),
                controlCapable ? QStringLiteral("ok") : (boardAlive ? QStringLiteral("warn") : QStringLiteral("error")),
                controlCapable ? QStringLiteral("CONTROL CAPABLE") : (boardAlive ? QStringLiteral("ALIVE/GATED") : QStringLiteral("NOT ALIVE")),
                controlCapable ? QStringLiteral("CAPABILITY + BOARD_HEALTH permit control")
                               : (blockReason.isEmpty() ? QStringLiteral("waiting for valid capability/health") : blockReason),
                controlCapable,
                !controlCapable);
    rows << row(QStringLiteral("bus"),
                QStringLiteral("Target BUS"),
                targetBusAllowed ? QStringLiteral("ok") : QStringLiteral("error"),
                targetBusAllowed ? QStringLiteral("TX ALLOWED") : QStringLiteral("TX BLOCKED"),
                targetBusAllowed ? QStringLiteral("role/capability resolved for control TX")
                                 : QStringLiteral("target BUS %1 unresolved or not TX allowed").arg(m_target.bus),
                targetBusAllowed,
                !targetBusAllowed);
    rows << row(QStringLiteral("arm"),
                QStringLiteral("ARM"),
                m_armed ? QStringLiteral("ok") : QStringLiteral("warn"),
                m_armed ? QStringLiteral("ARMED") : QStringLiteral("STANDBY"),
                m_armed ? QStringLiteral("motion commands may be requested") : QStringLiteral("neutral/manual target only until armed"),
                m_armed,
                false);
    rows << row(QStringLiteral("tx"),
                QStringLiteral("Actual TX"),
                actualTxConfirmed ? QStringLiteral("ok") : QStringLiteral("warn"),
                actualTxConfirmed ? QStringLiteral("CAN_TX_RAW") : QStringLiteral("NO AUDIT"),
                actualTxConfirmed ? QStringLiteral("last matching board CAN_TX_RAW confirms actual send")
                                  : QStringLiteral("ACK/write do not count as actual CAN success"),
                actualTxConfirmed,
                false);
    rows << row(QStringLiteral("fault"),
                QStringLiteral("Fault/block"),
                faultActive ? QStringLiteral("error") : QStringLiteral("ok"),
                faultActive ? QStringLiteral("ACTIVE") : QStringLiteral("CLEAR"),
                faultActive ? QStringLiteral("inspect fault/block stage before sending") : QStringLiteral("no active control fault/block event"),
                !faultActive,
                faultActive);
    return rows;
}

} // namespace CanMonitorControl
