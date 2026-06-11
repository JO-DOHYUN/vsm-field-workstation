#pragma once

#include "StableMapListModel.h"

#include <QMap>
#include <QVariantList>
#include <QVector>

namespace CanMonitorControl {

class ControlAuditModel {
public:
    ControlAuditModel();

    StableMapListModel* model() { return &m_model; }
    const StableMapListModel* model() const { return &m_model; }

    void reset();
    void noteHostFrameQueued();
    void noteHostWriteResult(bool ok);
    void noteAck(bool accepted);
    void rememberAcceptedAck(quint32 canId, quint32 commandId);
    quint32 takeAcceptedCommandId(quint32 canId);
    void noteTxAudit(bool matched);
    bool noteFeedbackIfDue(quint32 canId, qint64 nowWallMs, qint64 minIntervalMs = 250);
    bool ackUiDue(bool rejected, qint64 nowWallMs, qint64 minIntervalMs = 250);
    bool txAuditUiDue(bool unmatched, qint64 nowWallMs, qint64 minIntervalMs = 250);

    QString statsSummary() const;
    QVariantList stages() const;
    void appendEvent(const QString& stage,
                     const QString& level,
                     const QString& summary,
                     const QString& detail = QString(),
                     quint32 commandId = 0,
                     quint32 canId = 0,
                     quint8 bus = 0);

    QString lastRequestSummary() const { return m_lastRequestSummary; }
    QString lastHostWriteSummary() const { return m_lastHostWriteSummary; }
    QString lastAckSummary() const { return m_lastAckSummary; }
    QString lastAuditSummary() const { return m_lastAuditSummary; }
    QString lastFeedbackSummary() const { return m_lastFeedbackSummary; }
    QString lastFaultSummary() const { return m_lastFaultSummary; }
    QString lastHostWriteLevel() const { return m_lastHostWriteLevel; }
    QString lastAckLevel() const { return m_lastAckLevel; }
    QString lastAuditLevel() const { return m_lastAuditLevel; }
    QString lastFeedbackLevel() const { return m_lastFeedbackLevel; }
    QString lastFaultLevel() const { return m_lastFaultLevel; }
    bool actualTxConfirmed() const;
    bool faultActive() const;

private:
    static QString idText(quint32 id);
    quint64 pendingAuditCount() const;

    StableMapListModel m_model;
    QVector<QVariantMap> m_rows;
    QMap<quint32, QVector<quint32>> m_acceptedAckByCanId;
    QMap<quint32, qint64> m_feedbackLastWallMsByCanId;
    quint64 m_eventCounter = 0;
    quint64 m_hostFrameQueuedCount = 0;
    quint64 m_hostWriteOkCount = 0;
    quint64 m_hostWriteFailCount = 0;
    quint64 m_ackAcceptedCount = 0;
    quint64 m_ackRejectedCount = 0;
    quint64 m_txAuditMatchedCount = 0;
    quint64 m_txAuditUnmatchedCount = 0;
    quint64 m_feedbackObservedCount = 0;
    qint64 m_lastAckUiWallMs = 0;
    qint64 m_lastAuditUiWallMs = 0;
    QString m_lastRequestSummary = QStringLiteral("No host request queued yet");
    QString m_lastHostWriteSummary = QStringLiteral("No Qt serial write yet");
    QString m_lastAckSummary = QStringLiteral("No CONTROL_ACK yet");
    QString m_lastAuditSummary = QStringLiteral("No CAN_TX_RAW audit yet");
    QString m_lastFeedbackSummary = QStringLiteral("No feedback CAN_RX observed yet");
    QString m_lastFaultSummary = QStringLiteral("No control fault/block event yet");
    QString m_lastHostWriteLevel = QStringLiteral("info");
    QString m_lastAckLevel = QStringLiteral("info");
    QString m_lastAuditLevel = QStringLiteral("info");
    QString m_lastFeedbackLevel = QStringLiteral("info");
    QString m_lastFaultLevel = QStringLiteral("ok");
};

} // namespace CanMonitorControl
