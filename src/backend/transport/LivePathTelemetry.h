#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace CanMonitorTransport {

struct TelemetryRateWindow {
    QHash<QString, quint64> lastTotals;
    QHash<QString, qint64> lastMs;
    QHash<QString, double> rates;

    void reset() {
        lastTotals.clear();
        lastMs.clear();
        rates.clear();
    }

    double ratePerSec(const QString& key, quint64 total, qint64 nowMs) {
        const qint64 previousMs = lastMs.value(key, -1);
        if (previousMs >= 0 && nowMs > previousMs) {
            const quint64 previousTotal = lastTotals.value(key, total);
            const quint64 delta = total >= previousTotal ? total - previousTotal : 0;
            rates.insert(key, double(delta) * 1000.0 / double(nowMs - previousMs));
        } else if (!rates.contains(key)) {
            rates.insert(key, 0.0);
        }
        lastTotals.insert(key, total);
        lastMs.insert(key, nowMs);
        return rates.value(key, 0.0);
    }
};

inline void insertCounter(QJsonObject& out, const QString& key, quint64 value) {
    out.insert(key, QString::number(value));
}

struct LivePathTelemetry {
    quint64 parsedCanRx = 0;
    quint64 projectionInputFrames = 0;
    quint64 snapshotPendingKeys = 0;
    quint64 snapshotEmitted = 0;
    quint64 snapshotEmittedFrames = 0;
    bool snapshotInflight = false;
    quint64 snapshotAck = 0;
    quint64 snapshotCoalesced = 0;
    quint64 snapshotDropped = 0;

    quint64 projectedSignalReceived = 0;
    quint64 projectedFramesReceived = 0;
    quint64 projectionQueueKeys = 0;
    quint64 projectionFlushCount = 0;
    quint64 framesReceivedEmit = 0;
    quint64 framesReceivedFrames = 0;
    quint64 framesReceivedEmitSeq = 0;
    quint64 framesReceivedSlotSeq = 0;
    qint64 framesReceivedLastEmitWallMs = 0;
    qint64 framesReceivedLastSlotWallMs = 0;
    qint64 framesReceivedSlotDelayMs = -1;
    qint64 framesReceivedSlotDelayMaxMs = 0;

    quint64 appFramesReceivedCalls = 0;
    quint64 appFramesReceivedFrames = 0;
    quint64 legacyLiveRouteSuppressed = 0;
    quint64 appendPendingFrames = 0;
    quint64 pendingLiveRows = 0;
    quint64 liveFlushCalls = 0;
    quint64 liveFlushProcessed = 0;
    quint64 liveFlushTimerFireCount = 0;
    quint64 queueLiveViewCalls = 0;
    quint64 queueLiveViewFrames = 0;
    quint64 liveViewPausedDrops = 0;
    quint64 liveViewPanelDrops = 0;
    quint64 liveViewFlushCalls = 0;
    quint64 liveViewFlushTimerFireCount = 0;
    quint64 appendLiveBatchCalls = 0;
    quint64 appendLiveBatchFrames = 0;
    quint64 liveModelRows = 0;
    quint64 appSnapshotReceive = 0;
    quint64 appSnapshotAck = 0;
    quint64 appSnapshotApply = 0;
    quint64 appSnapshotApplyRows = 0;
    qint64 appSnapshotLastReceiveWallMs = 0;
    qint64 appSnapshotLastApplyWallMs = 0;
    qint64 appSnapshotApplyMaxMs = 0;
    bool liveFlushTimerActive = false;
    bool liveViewFlushTimerActive = false;
    bool livePanelActive = true;
    bool liveUiPaused = false;

    TelemetryRateWindow rates;

    QJsonObject toJson(qint64 nowMs) {
        QJsonObject out;
        insertCounter(out, QStringLiteral("parsed_can_rx"), parsedCanRx);
        out.insert(QStringLiteral("parsed_can_rx_per_sec"), rates.ratePerSec(QStringLiteral("parsed_can_rx"), parsedCanRx, nowMs));
        insertCounter(out, QStringLiteral("projection_input_frames"), projectionInputFrames);
        insertCounter(out, QStringLiteral("snapshot_pending_keys"), snapshotPendingKeys);
        insertCounter(out, QStringLiteral("snapshot_emitted"), snapshotEmitted);
        out.insert(QStringLiteral("snapshot_emitted_per_sec"), rates.ratePerSec(QStringLiteral("snapshot_emitted"), snapshotEmitted, nowMs));
        insertCounter(out, QStringLiteral("snapshot_emitted_frames"), snapshotEmittedFrames);
        out.insert(QStringLiteral("snapshot_inflight"), snapshotInflight);
        insertCounter(out, QStringLiteral("snapshot_ack"), snapshotAck);
        insertCounter(out, QStringLiteral("snapshot_coalesced"), snapshotCoalesced);
        insertCounter(out, QStringLiteral("snapshot_dropped"), snapshotDropped);

        insertCounter(out, QStringLiteral("projected_signal_received"), projectedSignalReceived);
        insertCounter(out, QStringLiteral("projected_frames_received"), projectedFramesReceived);
        insertCounter(out, QStringLiteral("projection_queue_keys"), projectionQueueKeys);
        insertCounter(out, QStringLiteral("projection_flush_count"), projectionFlushCount);
        insertCounter(out, QStringLiteral("frames_received_emit"), framesReceivedEmit);
        out.insert(QStringLiteral("frames_received_emit_per_sec"), rates.ratePerSec(QStringLiteral("frames_received_emit"), framesReceivedEmit, nowMs));
        insertCounter(out, QStringLiteral("frames_received_frames"), framesReceivedFrames);
        insertCounter(out, QStringLiteral("frames_received_emit_seq"), framesReceivedEmitSeq);
        insertCounter(out, QStringLiteral("framesReceived_slot_seq"), framesReceivedSlotSeq);
        insertCounter(out, QStringLiteral("frames_received_last_emit_wall_ms"), quint64(std::max<qint64>(0, framesReceivedLastEmitWallMs)));
        insertCounter(out, QStringLiteral("framesReceived_last_slot_wall_ms"), quint64(std::max<qint64>(0, framesReceivedLastSlotWallMs)));
        out.insert(QStringLiteral("framesReceived_slot_delay_ms"), framesReceivedSlotDelayMs);
        out.insert(QStringLiteral("framesReceived_slot_delay_max_ms"), framesReceivedSlotDelayMaxMs);

        insertCounter(out, QStringLiteral("framesReceived_calls"), appFramesReceivedCalls);
        insertCounter(out, QStringLiteral("framesReceived_frames"), appFramesReceivedFrames);
        insertCounter(out, QStringLiteral("legacy_live_route_suppressed"), legacyLiveRouteSuppressed);
        insertCounter(out, QStringLiteral("append_pending_frames"), appendPendingFrames);
        insertCounter(out, QStringLiteral("pending_live_rows"), pendingLiveRows);
        insertCounter(out, QStringLiteral("live_flush_calls"), liveFlushCalls);
        insertCounter(out, QStringLiteral("live_flush_processed"), liveFlushProcessed);
        insertCounter(out, QStringLiteral("live_flush_timer_fire_count"), liveFlushTimerFireCount);
        insertCounter(out, QStringLiteral("queue_live_view_calls"), queueLiveViewCalls);
        insertCounter(out, QStringLiteral("queue_live_view_frames"), queueLiveViewFrames);
        insertCounter(out, QStringLiteral("live_view_paused_drops"), liveViewPausedDrops);
        insertCounter(out, QStringLiteral("live_view_panel_drops"), liveViewPanelDrops);
        insertCounter(out, QStringLiteral("live_view_flush_calls"), liveViewFlushCalls);
        insertCounter(out, QStringLiteral("live_view_flush_timer_fire_count"), liveViewFlushTimerFireCount);
        insertCounter(out, QStringLiteral("append_live_batch_calls"), appendLiveBatchCalls);
        insertCounter(out, QStringLiteral("append_live_batch_frames"), appendLiveBatchFrames);
        insertCounter(out, QStringLiteral("live_model_rows"), liveModelRows);
        insertCounter(out, QStringLiteral("app_snapshot_receive"), appSnapshotReceive);
        insertCounter(out, QStringLiteral("app_snapshot_ack"), appSnapshotAck);
        insertCounter(out, QStringLiteral("app_snapshot_apply"), appSnapshotApply);
        insertCounter(out, QStringLiteral("app_snapshot_apply_rows"), appSnapshotApplyRows);
        insertCounter(out, QStringLiteral("app_snapshot_last_receive_wall_ms"), quint64(std::max<qint64>(0, appSnapshotLastReceiveWallMs)));
        insertCounter(out, QStringLiteral("app_snapshot_last_apply_wall_ms"), quint64(std::max<qint64>(0, appSnapshotLastApplyWallMs)));
        out.insert(QStringLiteral("app_snapshot_apply_max_ms"), appSnapshotApplyMaxMs);
        out.insert(QStringLiteral("liveFlushTimerActive"), liveFlushTimerActive);
        out.insert(QStringLiteral("liveViewFlushTimerActive"), liveViewFlushTimerActive);
        out.insert(QStringLiteral("m_livePanelActive"), livePanelActive);
        out.insert(QStringLiteral("m_liveUiPaused"), liveUiPaused);
        return out;
    }
};

struct DrainEventTelemetry {
    quint64 readyReadCalls = 0;
    quint64 bytesAvailableEmits = 0;
    quint64 readBurstBytesLast = 0;
    quint64 readBurstBytesMax = 0;
    quint64 readLoopsLast = 0;
    quint64 readLoopsMax = 0;
    quint64 drainQueueUsedBytes = 0;
    quint64 drainQueueMaxUsedBytes = 0;
    quint64 drainQueueCapacityBytes = 0;
    quint64 drainQueueOverrunBytes = 0;

    quint64 scheduleDrainPumpCalls = 0;
    quint64 typedWorkerInvokeRequests = 0;
    quint64 typedWorkerInvokeSuppressed = 0;
    quint64 drainStatusReceived = 0;
    bool drainPumpScheduledFlag = false;

    quint64 schedulePumpCalls = 0;
    quint64 schedulePumpIgnoredAlreadyScheduled = 0;
    quint64 pumpCalls = 0;
    quint64 pumpBlocks = 0;
    quint64 pumpBytes = 0;
    quint64 pumpRescheduleCount = 0;
    quint64 outputSignalCount = 0;
    quint64 statusSignalCount = 0;
    quint64 diagnosticsSignalCount = 0;
    quint64 captureQueueReadySignalCount = 0;
    quint64 typedStatusReadySignalCount = 0;
    quint64 truthStatusReadySignalCount = 0;
    quint64 projectionStatusReadySignalCount = 0;

    quint64 typedProjectionStatusEmit = 0;
    quint64 typedProjectionStatusReceive = 0;
    quint64 typedTruthStatusEmit = 0;
    quint64 typedTruthStatusReceive = 0;
    quint64 typedTransportStatusEmit = 0;
    quint64 typedTransportStatusReceive = 0;

    quint64 analysisHandoffPendingFrames = 0;
    quint64 analysisHandoffMaxPendingFrames = 0;
    quint64 analysisHandoffDispatchCount = 0;
    quint64 analysisHandoffDispatchFrames = 0;
    quint64 analysisHandoffCompleteCount = 0;
    quint64 analysisHandoffCompleteFrames = 0;
    quint64 analysisHandoffOverrunFrames = 0;
    bool analysisHandoffInflight = false;

    quint64 rawLedgerHandoffPendingFrames = 0;
    quint64 rawLedgerHandoffPendingBytes = 0;
    quint64 rawLedgerHandoffMaxPendingBytes = 0;
    quint64 rawLedgerHandoffDispatchCount = 0;
    quint64 rawLedgerHandoffDispatchFrames = 0;
    quint64 rawLedgerHandoffCompleteCount = 0;
    quint64 rawLedgerHandoffCompleteFrames = 0;
    quint64 rawLedgerHandoffOverrunBytes = 0;
    bool rawLedgerHandoffInflight = false;

    quint64 truthHandoffEmitCount = 0;
    quint64 truthHandoffEmitFrames = 0;
    quint64 truthHandoffPendingKeys = 0;
    quint64 truthHandoffFlushCount = 0;

    TelemetryRateWindow rates;

    QJsonObject toJson(qint64 nowMs) {
        QJsonObject out;
        insertCounter(out, QStringLiteral("readyRead_calls"), readyReadCalls);
        out.insert(QStringLiteral("readyRead_per_sec"), rates.ratePerSec(QStringLiteral("readyRead_calls"), readyReadCalls, nowMs));
        insertCounter(out, QStringLiteral("bytes_available_emits"), bytesAvailableEmits);
        out.insert(QStringLiteral("bytes_available_emit_per_sec"), rates.ratePerSec(QStringLiteral("bytes_available_emits"), bytesAvailableEmits, nowMs));
        insertCounter(out, QStringLiteral("read_burst_bytes_last"), readBurstBytesLast);
        insertCounter(out, QStringLiteral("read_burst_bytes_max"), readBurstBytesMax);
        insertCounter(out, QStringLiteral("read_loops_last"), readLoopsLast);
        insertCounter(out, QStringLiteral("read_loops_max"), readLoopsMax);
        insertCounter(out, QStringLiteral("drain_queue_used_bytes"), drainQueueUsedBytes);
        insertCounter(out, QStringLiteral("drain_queue_max_used_bytes"), drainQueueMaxUsedBytes);
        insertCounter(out, QStringLiteral("drain_queue_capacity_bytes"), drainQueueCapacityBytes);
        insertCounter(out, QStringLiteral("drain_queue_overrun_bytes"), drainQueueOverrunBytes);

        insertCounter(out, QStringLiteral("scheduleDrainPump_calls"), scheduleDrainPumpCalls);
        out.insert(QStringLiteral("scheduleDrainPump_per_sec"), rates.ratePerSec(QStringLiteral("scheduleDrainPump_calls"), scheduleDrainPumpCalls, nowMs));
        insertCounter(out, QStringLiteral("typed_worker_invoke_requests"), typedWorkerInvokeRequests);
        insertCounter(out, QStringLiteral("typed_worker_invoke_suppressed"), typedWorkerInvokeSuppressed);
        insertCounter(out, QStringLiteral("drain_status_received"), drainStatusReceived);
        out.insert(QStringLiteral("drain_pump_scheduled_flag"), drainPumpScheduledFlag);

        insertCounter(out, QStringLiteral("schedulePump_calls"), schedulePumpCalls);
        out.insert(QStringLiteral("schedulePump_per_sec"), rates.ratePerSec(QStringLiteral("schedulePump_calls"), schedulePumpCalls, nowMs));
        insertCounter(out, QStringLiteral("schedulePump_ignored_already_scheduled"), schedulePumpIgnoredAlreadyScheduled);
        insertCounter(out, QStringLiteral("pump_calls"), pumpCalls);
        out.insert(QStringLiteral("pump_per_sec"), rates.ratePerSec(QStringLiteral("pump_calls"), pumpCalls, nowMs));
        insertCounter(out, QStringLiteral("pump_blocks"), pumpBlocks);
        insertCounter(out, QStringLiteral("pump_bytes"), pumpBytes);
        insertCounter(out, QStringLiteral("pump_reschedule_count"), pumpRescheduleCount);
        insertCounter(out, QStringLiteral("output_signal_count"), outputSignalCount);
        out.insert(QStringLiteral("output_signal_per_sec"), rates.ratePerSec(QStringLiteral("output_signal_count"), outputSignalCount, nowMs));
        insertCounter(out, QStringLiteral("status_signal_count"), statusSignalCount);
        insertCounter(out, QStringLiteral("diagnostics_signal_count"), diagnosticsSignalCount);
        insertCounter(out, QStringLiteral("captureQueueReady_signal_count"), captureQueueReadySignalCount);
        insertCounter(out, QStringLiteral("typedStatusReady_signal_count"), typedStatusReadySignalCount);
        insertCounter(out, QStringLiteral("truthStatusReady_signal_count"), truthStatusReadySignalCount);
        insertCounter(out, QStringLiteral("projectionStatusReady_signal_count"), projectionStatusReadySignalCount);
        insertCounter(out, QStringLiteral("typedProjectionStatus_emit"), typedProjectionStatusEmit);
        insertCounter(out, QStringLiteral("typedProjectionStatus_receive"), typedProjectionStatusReceive);
        insertCounter(out, QStringLiteral("typedTruthStatus_emit"), typedTruthStatusEmit);
        insertCounter(out, QStringLiteral("typedTruthStatus_receive"), typedTruthStatusReceive);
        insertCounter(out, QStringLiteral("typedTransportStatus_emit"), typedTransportStatusEmit);
        insertCounter(out, QStringLiteral("typedTransportStatus_receive"), typedTransportStatusReceive);

        insertCounter(out, QStringLiteral("analysis_handoff_pending_frames"), analysisHandoffPendingFrames);
        insertCounter(out, QStringLiteral("analysis_handoff_max_pending_frames"), analysisHandoffMaxPendingFrames);
        insertCounter(out, QStringLiteral("analysis_handoff_dispatch_count"), analysisHandoffDispatchCount);
        insertCounter(out, QStringLiteral("analysis_handoff_dispatch_frames"), analysisHandoffDispatchFrames);
        insertCounter(out, QStringLiteral("analysis_handoff_complete_count"), analysisHandoffCompleteCount);
        insertCounter(out, QStringLiteral("analysis_handoff_complete_frames"), analysisHandoffCompleteFrames);
        insertCounter(out, QStringLiteral("analysis_handoff_overrun_frames"), analysisHandoffOverrunFrames);
        out.insert(QStringLiteral("analysis_handoff_inflight"), analysisHandoffInflight);

        insertCounter(out, QStringLiteral("raw_ledger_handoff_pending_frames"), rawLedgerHandoffPendingFrames);
        insertCounter(out, QStringLiteral("raw_ledger_handoff_pending_bytes"), rawLedgerHandoffPendingBytes);
        insertCounter(out, QStringLiteral("raw_ledger_handoff_max_pending_bytes"), rawLedgerHandoffMaxPendingBytes);
        insertCounter(out, QStringLiteral("raw_ledger_handoff_dispatch_count"), rawLedgerHandoffDispatchCount);
        insertCounter(out, QStringLiteral("raw_ledger_handoff_dispatch_frames"), rawLedgerHandoffDispatchFrames);
        insertCounter(out, QStringLiteral("raw_ledger_handoff_complete_count"), rawLedgerHandoffCompleteCount);
        insertCounter(out, QStringLiteral("raw_ledger_handoff_complete_frames"), rawLedgerHandoffCompleteFrames);
        insertCounter(out, QStringLiteral("raw_ledger_handoff_overrun_bytes"), rawLedgerHandoffOverrunBytes);
        out.insert(QStringLiteral("raw_ledger_handoff_inflight"), rawLedgerHandoffInflight);

        insertCounter(out, QStringLiteral("truth_handoff_emit_count"), truthHandoffEmitCount);
        insertCounter(out, QStringLiteral("truth_handoff_emit_frames"), truthHandoffEmitFrames);
        insertCounter(out, QStringLiteral("truth_handoff_pending_keys"), truthHandoffPendingKeys);
        insertCounter(out, QStringLiteral("truth_handoff_flush_count"), truthHandoffFlushCount);
        return out;
    }
};

} // namespace CanMonitorTransport
