#include "control/ControlAuditModel.h"

#include <QDateTime>

namespace CanMonitorControl {

ControlAuditModel::ControlAuditModel() {
    m_model.setRoles({QStringLiteral("key"),
                      QStringLiteral("timeText"),
                      QStringLiteral("stage"),
                      QStringLiteral("level"),
                      QStringLiteral("summary"),
                      QStringLiteral("detail"),
                      QStringLiteral("commandId"),
                      QStringLiteral("canId"),
                      QStringLiteral("bus")});
}

QString ControlAuditModel::idText(quint32 id) {
    return QStringLiteral("0X%1").arg(id, 0, 16).toUpper();
}

void ControlAuditModel::reset() {
    m_rows.clear();
    m_acceptedAckByCanId.clear();
    m_feedbackLastWallMsByCanId.clear();
    m_eventCounter = 0;
    m_hostFrameQueuedCount = 0;
    m_hostWriteOkCount = 0;
    m_hostWriteFailCount = 0;
    m_ackAcceptedCount = 0;
    m_ackRejectedCount = 0;
    m_txAuditMatchedCount = 0;
    m_txAuditUnmatchedCount = 0;
    m_feedbackObservedCount = 0;
    m_lastAckUiWallMs = 0;
    m_lastAuditUiWallMs = 0;
    m_lastRequestSummary = QStringLiteral("No host request queued yet");
    m_lastHostWriteSummary = QStringLiteral("No Qt serial write yet");
    m_lastAckSummary = QStringLiteral("No CONTROL_ACK yet");
    m_lastAuditSummary = QStringLiteral("No CAN_TX_RAW audit yet");
    m_lastFeedbackSummary = QStringLiteral("No feedback CAN_RX observed yet");
    m_lastFaultSummary = QStringLiteral("No control fault/block event yet");
    m_lastHostWriteLevel = QStringLiteral("info");
    m_lastAckLevel = QStringLiteral("info");
    m_lastAuditLevel = QStringLiteral("info");
    m_lastFeedbackLevel = QStringLiteral("info");
    m_lastFaultLevel = QStringLiteral("ok");
    m_model.clear();
}

void ControlAuditModel::noteHostFrameQueued() {
    ++m_hostFrameQueuedCount;
}

void ControlAuditModel::noteHostWriteResult(bool ok) {
    if (ok) ++m_hostWriteOkCount;
    else ++m_hostWriteFailCount;
}

void ControlAuditModel::noteAck(bool accepted) {
    if (accepted) ++m_ackAcceptedCount;
    else ++m_ackRejectedCount;
}

void ControlAuditModel::rememberAcceptedAck(quint32 canId, quint32 commandId) {
    if (canId == 0 || commandId == 0) return;
    m_acceptedAckByCanId[canId].push_back(commandId);
}

quint32 ControlAuditModel::takeAcceptedCommandId(quint32 canId) {
    auto it = m_acceptedAckByCanId.find(canId);
    if (it == m_acceptedAckByCanId.end() || it.value().isEmpty()) return 0;
    const quint32 commandId = it.value().takeFirst();
    if (it.value().isEmpty()) m_acceptedAckByCanId.erase(it);
    return commandId;
}

void ControlAuditModel::noteTxAudit(bool matched) {
    if (matched) ++m_txAuditMatchedCount;
    else ++m_txAuditUnmatchedCount;
}

bool ControlAuditModel::noteFeedbackIfDue(quint32 canId, qint64 nowWallMs, qint64 minIntervalMs) {
    const qint64 lastWallMs = m_feedbackLastWallMsByCanId.value(canId, 0);
    if (nowWallMs - lastWallMs < minIntervalMs) return false;
    m_feedbackLastWallMsByCanId[canId] = nowWallMs;
    ++m_feedbackObservedCount;
    return true;
}

bool ControlAuditModel::ackUiDue(bool rejected, qint64 nowWallMs, qint64 minIntervalMs) {
    if (rejected || m_lastAckUiWallMs <= 0 || nowWallMs - m_lastAckUiWallMs >= minIntervalMs) {
        m_lastAckUiWallMs = nowWallMs;
        return true;
    }
    return false;
}

bool ControlAuditModel::txAuditUiDue(bool unmatched, qint64 nowWallMs, qint64 minIntervalMs) {
    if (unmatched || m_lastAuditUiWallMs <= 0 || nowWallMs - m_lastAuditUiWallMs >= minIntervalMs) {
        m_lastAuditUiWallMs = nowWallMs;
        return true;
    }
    return false;
}

quint64 ControlAuditModel::pendingAuditCount() const {
    quint64 pending = 0;
    for (auto it = m_acceptedAckByCanId.cbegin(); it != m_acceptedAckByCanId.cend(); ++it) {
        pending += quint64(it.value().size());
    }
    return pending;
}

QString ControlAuditModel::statsSummary() const {
    return QStringLiteral("요청 %1 | Qt write OK/Fail %2/%3 | ACK OK/Reject %4/%5 | TX audit 대기/매칭/미매칭 %6/%7/%8 | feedback %9")
        .arg(m_hostFrameQueuedCount)
        .arg(m_hostWriteOkCount)
        .arg(m_hostWriteFailCount)
        .arg(m_ackAcceptedCount)
        .arg(m_ackRejectedCount)
        .arg(pendingAuditCount())
        .arg(m_txAuditMatchedCount)
        .arg(m_txAuditUnmatchedCount)
        .arg(m_feedbackObservedCount);
}

bool ControlAuditModel::actualTxConfirmed() const {
    return m_lastAuditLevel == QStringLiteral("ok");
}

bool ControlAuditModel::faultActive() const {
    return m_lastFaultLevel == QStringLiteral("error");
}

QVariantList ControlAuditModel::stages() const {
    auto stage = [](const QString& key,
                    const QString& title,
                    const QString& level,
                    const QString& summary,
                    const QString& evidence,
                    const QString& authority,
                    const QString& operatorText,
                    bool successAuthority) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("title"), title);
        row.insert(QStringLiteral("level"), level);
        row.insert(QStringLiteral("summary"), summary);
        row.insert(QStringLiteral("evidence"), evidence);
        row.insert(QStringLiteral("authority"), authority);
        row.insert(QStringLiteral("operatorText"), operatorText);
        row.insert(QStringLiteral("successAuthority"), successAuthority);
        return row;
    };

    return QVariantList{
        stage(QStringLiteral("request"),
              QStringLiteral("1 요청"),
              QStringLiteral("info"),
              m_lastRequestSummary,
              QStringLiteral("HOST_CAN_TX_REQUEST"),
              QStringLiteral("host_request"),
              QStringLiteral("운전자 의도/host 요청"),
              false),
        stage(QStringLiteral("write"),
              QStringLiteral("2 Qt write"),
              m_lastHostWriteLevel,
              m_lastHostWriteSummary,
              QStringLiteral("serial write result"),
              QStringLiteral("serial_only"),
              QStringLiteral("PC가 보드로 넘긴 상태"),
              false),
        stage(QStringLiteral("ack"),
              QStringLiteral("3 CONTROL_ACK"),
              m_lastAckLevel,
              m_lastAckSummary,
              QStringLiteral("board decision only"),
              QStringLiteral("board_acceptance_only"),
              QStringLiteral("보드 접수 증거, CAN 성공 아님"),
              false),
        stage(QStringLiteral("tx"),
              QStringLiteral("4 CAN_TX_RAW"),
              m_lastAuditLevel,
              m_lastAuditSummary,
              QStringLiteral("actual CAN TX success"),
              QStringLiteral("actual_can_tx"),
              QStringLiteral("실제 CAN 송신 성공 기준"),
              true),
        stage(QStringLiteral("feedback"),
              QStringLiteral("5 feedback"),
              m_lastFeedbackLevel,
              m_lastFeedbackSummary,
              QStringLiteral("CAN_RX feedback/echo candidate"),
              QStringLiteral("external_feedback"),
              QStringLiteral("외부 반응 후보, 송신 증거와 별도"),
              false),
        stage(QStringLiteral("fault"),
              QStringLiteral("6 fault/block"),
              m_lastFaultLevel,
              m_lastFaultSummary,
              QStringLiteral("gate, board event, write failure"),
              QStringLiteral("blocking_fault"),
              QStringLiteral("차단/실패 원인"),
              false)
    };
}

void ControlAuditModel::appendEvent(const QString& stage,
                                    const QString& level,
                                    const QString& summary,
                                    const QString& detail,
                                    quint32 commandId,
                                    quint32 canId,
                                    quint8 bus) {
    const QString stageText = detail.isEmpty() ? summary : detail;
    if (stage == QStringLiteral("REQUEST")) {
        m_lastRequestSummary = stageText;
    } else if (stage == QStringLiteral("HOST_WRITE")) {
        m_lastHostWriteSummary = stageText;
        m_lastHostWriteLevel = level == QStringLiteral("error") ? QStringLiteral("error") : QStringLiteral("ok");
        if (level == QStringLiteral("error")) {
            m_lastFaultSummary = stageText;
            m_lastFaultLevel = QStringLiteral("error");
        }
    } else if (stage == QStringLiteral("CONTROL_ACK")) {
        m_lastAckSummary = stageText;
        m_lastAckLevel = level == QStringLiteral("error") ? QStringLiteral("error") : QStringLiteral("info");
        if (level == QStringLiteral("error")) {
            m_lastFaultSummary = stageText;
            m_lastFaultLevel = QStringLiteral("error");
        }
    } else if (stage == QStringLiteral("CAN_TX_RAW")) {
        m_lastAuditSummary = stageText;
        m_lastAuditLevel = level == QStringLiteral("ok") ? QStringLiteral("ok") : QStringLiteral("warn");
        if (level == QStringLiteral("ok")) {
            m_lastFaultSummary = QStringLiteral("No active control fault/block event");
            m_lastFaultLevel = QStringLiteral("ok");
        }
    } else if (stage == QStringLiteral("CAN_RX_FEEDBACK")) {
        m_lastFeedbackSummary = stageText;
        m_lastFeedbackLevel = QStringLiteral("info");
    } else if (stage == QStringLiteral("BLOCKED") || stage == QStringLiteral("BOARD_EVENT")) {
        m_lastFaultSummary = stageText;
        m_lastFaultLevel = QStringLiteral("error");
    }

    const bool routineOk = level != QStringLiteral("error")
        && (stage == QStringLiteral("HOST_WRITE") ||
            stage == QStringLiteral("CONTROL_ACK") ||
            stage == QStringLiteral("CAN_TX_RAW"));
    if (routineOk && commandId > 0 && (commandId % 25U) != 0U) {
        return;
    }

    QVariantMap row;
    row.insert(QStringLiteral("key"), QStringLiteral("%1").arg(++m_eventCounter, 12, 10, QLatin1Char('0')));
    row.insert(QStringLiteral("timeText"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    row.insert(QStringLiteral("stage"), stage);
    row.insert(QStringLiteral("level"), level);
    row.insert(QStringLiteral("summary"), summary);
    row.insert(QStringLiteral("detail"), detail);
    row.insert(QStringLiteral("commandId"), commandId > 0 ? QStringLiteral("#%1").arg(commandId) : QString());
    row.insert(QStringLiteral("canId"), canId > 0 ? idText(canId) : QString());
    row.insert(QStringLiteral("bus"), canId > 0 ? QStringLiteral("BUS %1").arg(bus) : QString());

    m_rows.push_back(row);
    while (m_rows.size() > 80) {
        m_rows.removeFirst();
    }
    m_model.setRows(m_rows);
}

} // namespace CanMonitorControl
