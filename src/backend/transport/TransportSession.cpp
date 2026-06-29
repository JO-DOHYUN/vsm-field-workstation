#include "transport/TransportSession.h"

#include <algorithm>
#include <cmath>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>
#include <QVariantMap>

namespace CanMonitorTransport {

namespace {

QString traceText(const QJsonObject& trace, const QString& key, const QString& fallback = QStringLiteral("0")) {
    const QJsonValue value = trace.value(key);
    if (value.isUndefined() || value.isNull()) return fallback;
    if (value.isString()) return value.toString();
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::isfinite(number) && std::floor(number) == number) {
            return QString::number(qulonglong(number));
        }
        return QString::number(number, 'f', 1);
    }
    return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

quint64 traceCounter(const QJsonObject& trace, const QString& key) {
    const QJsonValue value = trace.value(key);
    if (value.isString()) return value.toString().toULongLong();
    if (value.isDouble() && value.toDouble() > 0.0) return quint64(value.toDouble());
    return 0;
}

bool traceFlag(const QJsonObject& trace, const QString& key) {
    const QJsonValue value = trace.value(key);
    if (value.isBool()) return value.toBool();
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        return text == QStringLiteral("true") || text == QStringLiteral("1");
    }
    return value.isDouble() && value.toDouble() != 0.0;
}

QString liveTraceLevel(const QJsonObject& trace) {
    const quint64 parsed = traceCounter(trace, QStringLiteral("parsed_can_rx"));
    const quint64 snapshotEmitted = traceCounter(trace, QStringLiteral("snapshot_emitted"));
    const quint64 snapshotAck = traceCounter(trace, QStringLiteral("snapshot_ack"));
    const quint64 serialEmit = traceCounter(trace, QStringLiteral("frames_received_emit"));
    const quint64 appCalls = traceCounter(trace, QStringLiteral("framesReceived_calls"));
    const quint64 appendLive = traceCounter(trace, QStringLiteral("append_live_batch_frames"));
    const quint64 modelRows = traceCounter(trace, QStringLiteral("live_model_rows"));
    const quint64 slotDelayMs = traceCounter(trace, QStringLiteral("framesReceived_slot_delay_ms"));
    const bool paused = traceFlag(trace, QStringLiteral("m_liveUiPaused"));
    const bool panelActive = traceFlag(trace, QStringLiteral("m_livePanelActive"));

    if (parsed > 0 && snapshotEmitted == 0) return QStringLiteral("WARN");
    if (snapshotEmitted > 0 && snapshotAck == 0) return QStringLiteral("WARN");
    if (serialEmit > 0 && appCalls == 0) return QStringLiteral("WARN");
    if (slotDelayMs > 1000) return QStringLiteral("WARN");
    if (appCalls > 0 && appendLive == 0 && !paused && panelActive) return QStringLiteral("WARN");
    if (appendLive > 0 && modelRows == 0) return QStringLiteral("WARN");
    return QStringLiteral("OK");
}

QString drainTraceLevel(const QJsonObject& trace) {
    if (traceCounter(trace, QStringLiteral("drain_queue_overrun_bytes")) > 0) return QStringLiteral("ERR");
    if (traceCounter(trace, QStringLiteral("analysis_handoff_overrun_frames")) > 0) return QStringLiteral("ERR");
    if (traceCounter(trace, QStringLiteral("raw_ledger_handoff_overrun_bytes")) > 0) return QStringLiteral("ERR");
    const quint64 readyRead = traceCounter(trace, QStringLiteral("readyRead_calls"));
    const quint64 pump = traceCounter(trace, QStringLiteral("pump_calls"));
    const quint64 scheduleIgnored = traceCounter(trace, QStringLiteral("schedulePump_ignored_already_scheduled"));
    const quint64 analysisPending = traceCounter(trace, QStringLiteral("analysis_handoff_pending_frames"));
    const quint64 rawPendingBytes = traceCounter(trace, QStringLiteral("raw_ledger_handoff_pending_bytes"));
    if (readyRead > 0 && pump == 0) return QStringLiteral("WARN");
    if (scheduleIgnored > pump && scheduleIgnored > 1024) return QStringLiteral("WARN");
    if (analysisPending > 8192 || rawPendingBytes > (4ULL * 1024ULL * 1024ULL)) return QStringLiteral("WARN");
    return QStringLiteral("OK");
}

} // namespace

void TransportSession::reset() {
    m_connected = false;
    m_typedFrames = 0;
    m_bytesDropped = 0;
    m_crcFailures = 0;
    m_lengthFailures = 0;
    m_versionWarnings = 0;
    m_seqGaps = 0;
    m_hostQueuedFrames = 0;
    m_hostQueuedBytes = 0;
    m_hostEnqueuedFrames = 0;
    m_hostWrittenFrames = 0;
    m_hostDroppedFrames = 0;
    m_captureActive = false;
    m_captureBytesWritten = 0;
    m_captureRecordCount = 0;
    m_boardCanDroppedTotal = 0;
    m_boardFifoOverflowTotal = 0;
    m_boardHealthAgeMs = -1;
    m_boardUplink = {};
    m_liveFrameAgeMs = -1;
    m_liveStatsAgeMs = -1;
    m_pendingLiveFrames = 0;
    m_sampledViewDrops = 0;
    m_projectedFrames = 0;
    m_sampledProjectionFrames = 0;
    m_droppedProjectionFrames = 0;
    m_observedControlEvidenceRecords = 0;
    m_projectedControlEvidenceRecords = 0;
    m_sampledControlEvidenceRecords = 0;
    m_maxProjectionBacklog = 0;
    m_flushBudgetHits = 0;
    m_lastFlushMs = 0;
    m_liveLatestObservedCanRxFrames = 0;
    m_liveLatestEmittedFrames = 0;
    m_liveLatestCoalescedUpdates = 0;
    m_liveLatestObservedBus0CanRxFrames = 0;
    m_liveLatestObservedBus1CanRxFrames = 0;
    m_liveLatestFlushCount = 0;
    m_liveLatestPendingKeys = 0;
    m_liveLatestMaxPendingKeys = 0;
    m_liveLatestLastInputRecords = 0;
    m_liveLatestLastOutputFrames = 0;
    m_liveLatestLastFlushMs = 0;
    m_displayLoss = 0;
    m_rawLedgerTotalRows = 0;
    m_rawLedgerVisibleRows = 0;
    m_rawLedgerSegmentBytes = 0;
    m_rawLedgerDroppedDisplayRows = 0;
    m_rawLedgerLatestSeq = 0;
    m_drainBytesTotal = 0;
    m_drainReadyReadCount = 0;
    m_drainReadyReadMaxUs = 0;
    m_drainBurstMaxBytes = 0;
    m_rawQueueUsedBytes = 0;
    m_rawQueueMaxUsedBytes = 0;
    m_rawQueueCapacityBytes = 0;
    m_rawQueueOverrunBytes = 0;
    m_rawQueueContentionCount = 0;
    m_parseBacklogBytes = 0;
    m_parserBatchMaxMs = 0;
    m_captureWriterQueueBytes = 0;
    m_captureWriterMaxQueueBytes = 0;
    m_captureWriterOverrunBytes = 0;
    m_captureWriteMaxMs = 0;
    m_analysisQueuedFrames = 0;
    m_analysisMaxQueuedFrames = 0;
    m_analysisCapacityFrames = 0;
    m_analysisEnqueuedFrames = 0;
    m_analysisProcessedFrames = 0;
    m_analysisOverrunFrames = 0;
    m_analysisPumpCount = 0;
    m_analysisPumpMaxMs = 0;
    m_analysisSnapshotMaxMs = 0;
    m_analysisTruthLoss = 0;
    m_runtimeProfileKey = QStringLiteral("passive_product");
    m_vehicleImpactState = QStringLiteral("blocked_unknown");
    m_serialOpenMode = QStringLiteral("read_only");
    m_dtrPolicy = QStringLiteral("no_touch");
    m_rtsPolicy = QStringLiteral("no_touch");
    m_hostTxEnabled = false;
    m_controlEnabled = false;
    m_labGatewayEnabled = false;
    m_boardEventTotal = 0;
    m_mcp2515EventTotal = 0;
    m_boardEventFatalTotal = 0;
    m_lastBoardEventCode = 0;
    m_lastBoardEventDetail = 0;
    m_lastBoardEventCounter = 0;
    m_lastBoardEventMonoUs = 0;
    m_mcp2515Details.clear();
    m_livePathTrace = {};
    m_drainEventTrace = {};
}

void TransportSession::setConnected(bool connected) {
    m_connected = connected;
    if (!connected) {
        m_hostQueuedFrames = 0;
        m_hostQueuedBytes = 0;
        m_liveFrameAgeMs = -1;
        m_liveStatsAgeMs = -1;
        m_pendingLiveFrames = 0;
    }
}

void TransportSession::updateTypedStatus(quint64 frames,
                                         quint64 bytesDropped,
                                         quint64 crcFailures,
                                         quint64 lengthFailures,
                                         quint64 versionWarnings,
                                         quint64 seqGaps) {
    m_typedFrames = frames;
    m_bytesDropped = bytesDropped;
    m_crcFailures = crcFailures;
    m_lengthFailures = lengthFailures;
    m_versionWarnings = versionWarnings;
    m_seqGaps = seqGaps;
}

void TransportSession::updateHostTxQueue(quint64 queuedFrames,
                                         quint64 queuedBytes,
                                         quint64 enqueuedFrames,
                                         quint64 writtenFrames,
                                         quint64 droppedFrames) {
    m_hostQueuedFrames = queuedFrames;
    m_hostQueuedBytes = queuedBytes;
    m_hostEnqueuedFrames = enqueuedFrames;
    m_hostWrittenFrames = writtenFrames;
    m_hostDroppedFrames = droppedFrames;
}

void TransportSession::updateCaptureStorage(bool active, quint64 bytesWritten, quint64 recordCount) {
    m_captureActive = active;
    m_captureBytesWritten = bytesWritten;
    m_captureRecordCount = recordCount;
}

void TransportSession::updateBoardHealth(quint32 canDroppedTotal,
                                         quint32 fifoOverflowTotal,
                                         qint64 statsAgeMs,
                                         const BoardUplinkCounters& uplinkCounters) {
    m_boardCanDroppedTotal = canDroppedTotal;
    m_boardFifoOverflowTotal = fifoOverflowTotal;
    m_boardHealthAgeMs = statsAgeMs;
    m_boardUplink = uplinkCounters;
}

void TransportSession::updateLiveRuntime(qint64 nowWallMs,
                                         qint64 lastFrameWallMs,
                                         qint64 lastStatsWallMs,
                                         int pendingFrames,
                                         quint64 sampledViewDrops,
                                         quint64 projectedFrames,
                                         quint64 sampledProjectionFrames,
                                         quint64 droppedProjectionFrames,
                                         quint64 observedControlEvidenceRecords,
                                         quint64 projectedControlEvidenceRecords,
                                         quint64 sampledControlEvidenceRecords,
                                         int maxProjectionBacklog,
                                         quint64 flushBudgetHits,
                                         int lastFlushMs) {
    m_liveFrameAgeMs = lastFrameWallMs > 0 ? nowWallMs - lastFrameWallMs : -1;
    m_liveStatsAgeMs = lastStatsWallMs > 0 ? nowWallMs - lastStatsWallMs : -1;
    m_pendingLiveFrames = pendingFrames;
    m_sampledViewDrops = sampledViewDrops;
    m_projectedFrames = projectedFrames;
    m_sampledProjectionFrames = sampledProjectionFrames;
    m_droppedProjectionFrames = droppedProjectionFrames;
    m_observedControlEvidenceRecords = observedControlEvidenceRecords;
    m_projectedControlEvidenceRecords = projectedControlEvidenceRecords;
    m_sampledControlEvidenceRecords = sampledControlEvidenceRecords;
    m_maxProjectionBacklog = maxProjectionBacklog;
    m_flushBudgetHits = flushBudgetHits;
    m_lastFlushMs = lastFlushMs;
}

void TransportSession::updateLiveLatest(quint64 observedCanRxFrames,
                                       quint64 emittedLatestFrames,
                                       quint64 coalescedLatestUpdates,
                                       quint64 observedBus0CanRxFrames,
                                       quint64 observedBus1CanRxFrames,
                                       quint64 flushCount,
                                       int pendingKeys,
                                       int maxPendingKeys,
                                       int lastInputRecords,
                                       int lastOutputFrames,
                                       int lastFlushMs,
                                       quint64 displayLoss) {
    m_liveLatestObservedCanRxFrames = observedCanRxFrames;
    m_liveLatestEmittedFrames = emittedLatestFrames;
    m_liveLatestCoalescedUpdates = coalescedLatestUpdates;
    m_liveLatestObservedBus0CanRxFrames = observedBus0CanRxFrames;
    m_liveLatestObservedBus1CanRxFrames = observedBus1CanRxFrames;
    m_liveLatestFlushCount = flushCount;
    m_liveLatestPendingKeys = pendingKeys;
    m_liveLatestMaxPendingKeys = maxPendingKeys;
    m_liveLatestLastInputRecords = lastInputRecords;
    m_liveLatestLastOutputFrames = lastOutputFrames;
    m_liveLatestLastFlushMs = lastFlushMs;
    m_displayLoss = displayLoss;
}

void TransportSession::updateRawLedger(quint64 totalRows,
                                       quint64 visibleRows,
                                       quint64 segmentBytes,
                                       quint64 droppedDisplayRows,
                                       quint64 latestSeq) {
    m_rawLedgerTotalRows = totalRows;
    m_rawLedgerVisibleRows = visibleRows;
    m_rawLedgerSegmentBytes = segmentBytes;
    m_rawLedgerDroppedDisplayRows = droppedDisplayRows;
    m_rawLedgerLatestSeq = latestSeq;
}

void TransportSession::updateDrainPipeline(quint64 bytesTotal,
                                           quint64 readyReadCount,
                                           quint64 readyReadMaxUs,
                                           quint64 drainBurstMaxBytes,
                                           quint64 rawQueueUsedBytes,
                                           quint64 rawQueueMaxUsedBytes,
                                           quint64 rawQueueCapacityBytes,
                                           quint64 rawQueueOverrunBytes,
                                           quint64 rawQueueContentionCount,
                                           quint64 parseBacklogBytes,
                                           quint64 parserBatchMaxMs,
                                           quint64 captureWriterQueueBytes,
                                           quint64 captureWriterMaxQueueBytes,
                                           quint64 captureWriterOverrunBytes,
                                           quint64 captureWriteMaxMs) {
    m_drainBytesTotal = bytesTotal;
    m_drainReadyReadCount = readyReadCount;
    m_drainReadyReadMaxUs = readyReadMaxUs;
    m_drainBurstMaxBytes = drainBurstMaxBytes;
    m_rawQueueUsedBytes = rawQueueUsedBytes;
    m_rawQueueMaxUsedBytes = rawQueueMaxUsedBytes;
    m_rawQueueCapacityBytes = rawQueueCapacityBytes;
    m_rawQueueOverrunBytes = rawQueueOverrunBytes;
    m_rawQueueContentionCount = rawQueueContentionCount;
    m_parseBacklogBytes = parseBacklogBytes;
    m_parserBatchMaxMs = parserBatchMaxMs;
    m_captureWriterQueueBytes = captureWriterQueueBytes;
    m_captureWriterMaxQueueBytes = captureWriterMaxQueueBytes;
    m_captureWriterOverrunBytes = captureWriterOverrunBytes;
    m_captureWriteMaxMs = captureWriteMaxMs;
}

void TransportSession::updateCoreTransportSummary(const QJsonObject& payload) {
    if (payload.contains(QStringLiteral("runtime_profile"))) {
        m_runtimeProfileKey = payload.value(QStringLiteral("runtime_profile")).toString(m_runtimeProfileKey);
    }
    if (payload.contains(QStringLiteral("vehicle_impact_state"))) {
        m_vehicleImpactState = payload.value(QStringLiteral("vehicle_impact_state")).toString(m_vehicleImpactState);
    }
    const QJsonObject policy = payload.value(QStringLiteral("transport_policy")).toObject();
    if (!policy.isEmpty()) {
        m_serialOpenMode = policy.value(QStringLiteral("serial_open_mode")).toString(m_serialOpenMode);
        m_dtrPolicy = policy.value(QStringLiteral("dtr_policy")).toString(m_dtrPolicy);
        m_rtsPolicy = policy.value(QStringLiteral("rts_policy")).toString(m_rtsPolicy);
        m_dtrSessionOnly = policy.value(QStringLiteral("dtr_session_only")).toBool(m_dtrSessionOnly);
        m_hostTxEnabled = policy.value(QStringLiteral("host_tx_enabled")).toBool(m_hostTxEnabled);
        m_controlEnabled = policy.value(QStringLiteral("control_enabled")).toBool(m_controlEnabled);
        m_labGatewayEnabled = policy.value(QStringLiteral("lab_gateway_enabled")).toBool(m_labGatewayEnabled);
    }

    if (payload.contains(QStringLiteral("typed_frames")) ||
        payload.contains(QStringLiteral("typed_crc_failures")) ||
        payload.contains(QStringLiteral("typed_length_failures"))) {
        updateTypedStatus(traceCounter(payload, QStringLiteral("typed_frames")),
                          traceCounter(payload, QStringLiteral("typed_bytes_dropped")),
                          traceCounter(payload, QStringLiteral("typed_crc_failures")),
                          traceCounter(payload, QStringLiteral("typed_length_failures")),
                          traceCounter(payload, QStringLiteral("typed_version_warnings")),
                          traceCounter(payload, QStringLiteral("typed_seq_gaps")));
    }

    if (payload.contains(QStringLiteral("projection_projected_can_rx")) ||
        payload.contains(QStringLiteral("projection_sampled_can_rx")) ||
        payload.contains(QStringLiteral("projection_dropped_can_rx"))) {
        m_projectedFrames = traceCounter(payload, QStringLiteral("projection_projected_can_rx"));
        m_sampledProjectionFrames = traceCounter(payload, QStringLiteral("projection_sampled_can_rx"));
        m_droppedProjectionFrames = traceCounter(payload, QStringLiteral("projection_dropped_can_rx"));
        m_observedControlEvidenceRecords = traceCounter(payload, QStringLiteral("projection_observed_control"));
        m_projectedControlEvidenceRecords = traceCounter(payload, QStringLiteral("projection_projected_control"));
        m_sampledControlEvidenceRecords = traceCounter(payload, QStringLiteral("projection_sampled_control"));
    }

    if (payload.contains(QStringLiteral("latest_observed_can_rx")) ||
        payload.contains(QStringLiteral("latest_emitted_frames")) ||
        payload.contains(QStringLiteral("latest_display_loss"))) {
        updateLiveLatest(traceCounter(payload, QStringLiteral("latest_observed_can_rx")),
                         traceCounter(payload, QStringLiteral("latest_emitted_frames")),
                         traceCounter(payload, QStringLiteral("latest_coalesced_updates")),
                         traceCounter(payload, QStringLiteral("latest_observed_bus0_can_rx")),
                         traceCounter(payload, QStringLiteral("latest_observed_bus1_can_rx")),
                         traceCounter(payload, QStringLiteral("latest_flush_count")),
                         int(traceCounter(payload, QStringLiteral("latest_pending_keys"))),
                         int(traceCounter(payload, QStringLiteral("latest_max_pending_keys"))),
                         int(traceCounter(payload, QStringLiteral("latest_last_input_records"))),
                         int(traceCounter(payload, QStringLiteral("latest_last_output_frames"))),
                         int(traceCounter(payload, QStringLiteral("latest_last_flush_ms"))),
                         traceCounter(payload, QStringLiteral("latest_display_loss")));
    }

    const QJsonObject drain = payload.value(QStringLiteral("drain_event_trace")).toObject();
    if (!drain.isEmpty()) {
        updateDrainEventTrace(drain);
        updateDrainPipeline(traceCounter(drain, QStringLiteral("drain_bytes_total")),
                            traceCounter(drain, QStringLiteral("ready_read_count")),
                            traceCounter(drain, QStringLiteral("ready_read_max_us")),
                            traceCounter(drain, QStringLiteral("drain_burst_max_bytes")),
                            traceCounter(drain, QStringLiteral("drain_queue_used_bytes")),
                            traceCounter(drain, QStringLiteral("drain_queue_max_used_bytes")),
                            traceCounter(drain, QStringLiteral("drain_queue_capacity_bytes")),
                            traceCounter(drain, QStringLiteral("drain_queue_overrun_bytes")),
                            traceCounter(drain, QStringLiteral("drain_queue_contention_count")),
                            traceCounter(drain, QStringLiteral("parse_backlog_bytes")),
                            traceCounter(drain, QStringLiteral("parser_batch_max_ms")),
                            traceCounter(drain, QStringLiteral("capture_writer_queue_bytes")),
                            traceCounter(drain, QStringLiteral("capture_writer_max_queue_bytes")),
                            traceCounter(drain, QStringLiteral("capture_writer_overrun_bytes")),
                            traceCounter(drain, QStringLiteral("capture_write_max_ms")));
    }
}

void TransportSession::updateAnalysisQueue(quint64 queuedFrames,
                                           quint64 maxQueuedFrames,
                                           quint64 capacityFrames,
                                           quint64 enqueuedFrames,
                                           quint64 processedFrames,
                                           quint64 overrunFrames,
                                           quint64 pumpCount,
                                           quint64 pumpMaxMs,
                                           quint64 snapshotMaxMs,
                                           quint64 truthLoss) {
    m_analysisQueuedFrames = queuedFrames;
    m_analysisMaxQueuedFrames = maxQueuedFrames;
    m_analysisCapacityFrames = capacityFrames;
    m_analysisEnqueuedFrames = enqueuedFrames;
    m_analysisProcessedFrames = processedFrames;
    m_analysisOverrunFrames = overrunFrames;
    m_analysisPumpCount = pumpCount;
    m_analysisPumpMaxMs = pumpMaxMs;
    m_analysisSnapshotMaxMs = snapshotMaxMs;
    m_analysisTruthLoss = truthLoss;
}

void TransportSession::updateLivePathTrace(const QJsonObject& trace) {
    m_livePathTrace = trace;
}

void TransportSession::updateDrainEventTrace(const QJsonObject& trace) {
    m_drainEventTrace = trace;
}

void TransportSession::noteBoardEvent(quint16 code, quint16 detail, quint32 counter, quint64 monoUs) {
    ++m_boardEventTotal;
    m_lastBoardEventCode = code;
    m_lastBoardEventDetail = detail;
    m_lastBoardEventCounter = counter;
    m_lastBoardEventMonoUs = monoUs;
    if (code == 9) {
        ++m_mcp2515EventTotal;
        m_mcp2515Details[detail] = m_mcp2515Details.value(detail) + 1;
    }
    if (code == 12 || code == 17) {
        ++m_boardEventFatalTotal;
    }
}

quint64 TransportSession::parserFaultCount() const {
    return m_bytesDropped + m_crcFailures + m_lengthFailures + m_versionWarnings + m_seqGaps + m_displayLoss + m_analysisTruthLoss;
}

QString TransportSession::liveLevel() const {
    if (!m_connected) return QStringLiteral("INFO");
    if (m_liveFrameAgeMs >= 0 && m_liveFrameAgeMs <= 1200) {
        if (m_droppedProjectionFrames > 0) return QStringLiteral("WARN");
        if (m_pendingLiveFrames > 4096 || m_sampledViewDrops > 0 || m_sampledProjectionFrames > 0) return QStringLiteral("WARN");
        return QStringLiteral("OK");
    }
    if (m_liveStatsAgeMs >= 0 && m_liveStatsAgeMs <= 2000) return QStringLiteral("WARN");
    return QStringLiteral("WARN");
}

QString TransportSession::boardUplinkLevel() const {
    if (!m_boardUplink.present) return QStringLiteral("INFO");
    if (m_boardUplink.serialRingClearTotal > 0 ||
        m_boardUplink.serialRingClearedBytesTotal > 0 ||
        m_boardUplink.canSegmentEnqueueFailTotal > 0 ||
        m_boardUplink.canTruthPoolAllocFailTotal > 0) {
        return QStringLiteral("ERR");
    }
    if (m_boardUplink.serialEnqueueFailTotal > 0 ||
        m_boardUplink.serialBackpressureTotal > 0 ||
        m_boardUplink.mcpDrainBudgetHitTotal > 0 ||
        m_boardUplink.uplinkPoolAllocFailTotal > 0) {
        return QStringLiteral("WARN");
    }
    return QStringLiteral("OK");
}

QString TransportSession::boardEventLevel() const {
    if (m_boardEventFatalTotal > 0) return QStringLiteral("ERR");
    if (m_mcp2515EventTotal > 0) return QStringLiteral("WARN");
    if (m_boardEventTotal > 0) return QStringLiteral("INFO");
    return QStringLiteral("OK");
}

QString TransportSession::level() const {
    const bool dtrPolicyAllowed = m_dtrPolicy == QStringLiteral("no_touch") ||
                                  (m_dtrPolicy == QStringLiteral("assert_true") && m_dtrSessionOnly);
    const bool passiveUnsafePolicy = m_serialOpenMode != QStringLiteral("read_only") ||
                                     !dtrPolicyAllowed ||
                                     m_rtsPolicy != QStringLiteral("no_touch") ||
                                     m_hostTxEnabled ||
                                     m_controlEnabled ||
                                     m_labGatewayEnabled;
    if (parserFaultCount() > 0 ||
        m_hostDroppedFrames > 0 ||
        m_rawQueueOverrunBytes > 0 ||
        m_captureWriterOverrunBytes > 0 ||
        m_analysisOverrunFrames > 0 ||
        passiveUnsafePolicy) {
        return QStringLiteral("ERR");
    }
    if (boardUplinkLevel() == QStringLiteral("ERR")) return QStringLiteral("ERR");
    if (boardEventLevel() == QStringLiteral("ERR")) return QStringLiteral("ERR");
    if (m_hostQueuedFrames > 64 || m_hostQueuedBytes > 16 * 1024) return QStringLiteral("WARN");
    if (boardUplinkLevel() == QStringLiteral("WARN")) return QStringLiteral("WARN");
    if (boardEventLevel() == QStringLiteral("WARN")) return QStringLiteral("WARN");
    if (liveLevel() == QStringLiteral("WARN")) return QStringLiteral("WARN");
    return QStringLiteral("OK");
}

QString TransportSession::liveStateText() const {
    if (!m_connected) return QStringLiteral("not connected");
    if (m_liveFrameAgeMs >= 0 && m_liveFrameAgeMs <= 1200) return QStringLiteral("frames live age %1ms").arg(m_liveFrameAgeMs);
    if (m_liveStatsAgeMs >= 0 && m_liveStatsAgeMs <= 2000) return QStringLiteral("stats live age %1ms, frame delayed").arg(m_liveStatsAgeMs);
    return QStringLiteral("connected but no recent live frame");
}

QString TransportSession::summary() const {
    QStringList parts;
    parts << QStringLiteral("transport %1").arg(level());
    parts << QStringLiteral("profile %1/%2").arg(m_runtimeProfileKey, m_vehicleImpactState);
    parts << QStringLiteral("typed frames %1 faults %2").arg(m_typedFrames).arg(parserFaultCount());
    if (m_drainBytesTotal > 0 || m_rawQueueMaxUsedBytes > 0) {
        parts << QStringLiteral("drain bytes %1 raw_q %2/%3 max %4")
                     .arg(m_drainBytesTotal)
                     .arg(m_rawQueueUsedBytes)
                     .arg(m_rawQueueCapacityBytes)
                     .arg(m_rawQueueMaxUsedBytes);
    }
    parts << QStringLiteral("hostTX q %1/%2B written %3 drop %4")
                 .arg(m_hostQueuedFrames)
                 .arg(m_hostQueuedBytes)
                 .arg(m_hostWrittenFrames)
                 .arg(m_hostDroppedFrames);
    parts << liveStateText();
    if (m_pendingLiveFrames > 0) parts << QStringLiteral("pending live %1").arg(m_pendingLiveFrames);
    if (m_sampledProjectionFrames > 0 || m_sampledViewDrops > 0 || m_sampledControlEvidenceRecords > 0) {
        parts << QStringLiteral("projection sampled %1 view %2 control %3")
                     .arg(m_sampledProjectionFrames)
                     .arg(m_sampledViewDrops)
                     .arg(m_sampledControlEvidenceRecords);
    }
    if (m_droppedProjectionFrames > 0) parts << QStringLiteral("projection dropped %1").arg(m_droppedProjectionFrames);
    if (m_analysisCapacityFrames > 0) {
        parts << QStringLiteral("analysis_q %1/%2 max %3 overrun %4")
                     .arg(m_analysisQueuedFrames)
                     .arg(m_analysisCapacityFrames)
                     .arg(m_analysisMaxQueuedFrames)
                     .arg(m_analysisOverrunFrames);
    }
    if (m_boardUplink.present) {
        parts << QStringLiteral("csm uplink clear %1 bp %2 seg_fail %3")
                     .arg(m_boardUplink.serialRingClearTotal)
                     .arg(m_boardUplink.serialBackpressureTotal)
                     .arg(m_boardUplink.canSegmentEnqueueFailTotal);
    }
    if (m_mcp2515EventTotal > 0) {
        parts << QStringLiteral("mcp2515 events %1 last 0x%2")
                     .arg(m_mcp2515EventTotal)
                     .arg(m_lastBoardEventDetail, 4, 16, QLatin1Char('0')).toUpper();
    }
    if (m_rawLedgerTotalRows > 0) parts << QStringLiteral("decoded tail %1 rows").arg(m_rawLedgerTotalRows);
    return parts.join(QStringLiteral(" | "));
}

QString TransportSession::boardEventDetailText() const {
    QStringList topDetails;
    QVector<QPair<quint16, quint64>> details;
    details.reserve(m_mcp2515Details.size());
    for (auto it = m_mcp2515Details.cbegin(); it != m_mcp2515Details.cend(); ++it) {
        details.push_back(qMakePair(it.key(), it.value()));
    }
    std::sort(details.begin(), details.end(), [](const auto& left, const auto& right) {
        if (left.second != right.second) return left.second > right.second;
        return left.first < right.first;
    });
    for (int index = 0; index < details.size() && index < 6; ++index) {
        topDetails << QStringLiteral("0x%1:%2")
                          .arg(details.at(index).first, 4, 16, QLatin1Char('0')).toUpper()
                          .arg(details.at(index).second);
    }
    return QStringLiteral("total %1 mcp2515 %2 fatal %3 last code %4 detail 0x%5 counter %6 mono_us %7 top %8")
        .arg(m_boardEventTotal)
        .arg(m_mcp2515EventTotal)
        .arg(m_boardEventFatalTotal)
        .arg(m_lastBoardEventCode)
        .arg(m_lastBoardEventDetail, 4, 16, QLatin1Char('0')).toUpper()
        .arg(m_lastBoardEventCounter)
        .arg(m_lastBoardEventMonoUs)
        .arg(topDetails.isEmpty() ? QStringLiteral("none") : topDetails.join(QStringLiteral(", ")));
}

QVariantList TransportSession::rows() const {
    auto row = [](const QString& key,
                  const QString& title,
                  const QString& level,
                  const QString& value,
                  const QString& detail,
                  bool blocking) {
        QVariantMap out;
        out.insert(QStringLiteral("key"), key);
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("level"), level);
        out.insert(QStringLiteral("value"), value);
        out.insert(QStringLiteral("detail"), detail);
        out.insert(QStringLiteral("blocking"), blocking);
        return out;
    };

    const quint64 parserFaults = parserFaultCount();
    const QString parserLevel = parserFaults > 0 ? QStringLiteral("ERR") : QStringLiteral("OK");
    const QString drainLevel = (m_rawQueueOverrunBytes > 0 || m_captureWriterOverrunBytes > 0) ? QStringLiteral("ERR")
        : (m_rawQueueCapacityBytes > 0 && m_rawQueueMaxUsedBytes > (m_rawQueueCapacityBytes * 3 / 4) ? QStringLiteral("WARN") : QStringLiteral("OK"));
    const QString hostLevel = m_hostDroppedFrames > 0 ? QStringLiteral("ERR")
        : (m_hostQueuedFrames > 64 || m_hostQueuedBytes > 16 * 1024 ? QStringLiteral("WARN") : QStringLiteral("OK"));
    const QString captureLevel = m_captureActive || m_captureRecordCount > 0 ? QStringLiteral("OK") : QStringLiteral("INFO");
    const QString boardLevel = (m_boardCanDroppedTotal > 0 || m_boardFifoOverflowTotal > 0) ? QStringLiteral("ERR")
        : (m_boardHealthAgeMs >= 0 && m_boardHealthAgeMs <= 2000 ? QStringLiteral("OK") : QStringLiteral("WARN"));
    const QString projectionLevel = m_droppedProjectionFrames > 0 ? QStringLiteral("WARN")
        : (m_sampledProjectionFrames > 0 || m_sampledViewDrops > 0 || m_pendingLiveFrames > 4096 ? QStringLiteral("WARN") : QStringLiteral("OK"));
    const QString latestLevel = m_displayLoss > 0 ? QStringLiteral("ERR")
        : (m_liveLatestPendingKeys > 4096 ? QStringLiteral("WARN") : QStringLiteral("OK"));
    const QString rawLedgerLevel = m_rawLedgerDroppedDisplayRows > 0 ? QStringLiteral("WARN") : QStringLiteral("OK");
    const QString uplinkLevel = boardUplinkLevel();
    const QString eventLevel = boardEventLevel();
    const QString liveTraceRowLevel = liveTraceLevel(m_livePathTrace);
    const QString drainTraceRowLevel = drainTraceLevel(m_drainEventTrace);
    const QString analysisQueueLevel = m_analysisOverrunFrames > 0 || m_analysisTruthLoss > 0 ? QStringLiteral("ERR")
        : (m_analysisCapacityFrames > 0 && m_analysisMaxQueuedFrames > (m_analysisCapacityFrames * 3 / 4) ? QStringLiteral("WARN") : QStringLiteral("OK"));
    const bool dtrPolicyAllowed = m_dtrPolicy == QStringLiteral("no_touch") ||
                                  (m_dtrPolicy == QStringLiteral("assert_true") && m_dtrSessionOnly);
    const bool passiveUnsafePolicy = m_serialOpenMode != QStringLiteral("read_only") ||
                                     !dtrPolicyAllowed ||
                                     m_rtsPolicy != QStringLiteral("no_touch") ||
                                     m_hostTxEnabled ||
                                     m_controlEnabled ||
                                     m_labGatewayEnabled;
    const QString profileLevel = passiveUnsafePolicy ? QStringLiteral("ERR")
        : (m_vehicleImpactState == QStringLiteral("verified_passive") ? QStringLiteral("OK") : QStringLiteral("WARN"));

    return QVariantList{
        row(QStringLiteral("passive_safety_profile"),
            QStringLiteral("Passive safety profile"),
            profileLevel,
            QStringLiteral("%1 / %2").arg(m_runtimeProfileKey, m_vehicleImpactState),
            QStringLiteral("serial %1 dtr %2 rts %3 host_tx %4 control %5 lab_gateway %6")
                .arg(m_serialOpenMode,
                     m_dtrSessionOnly ? QStringLiteral("%1(session-only)").arg(m_dtrPolicy) : m_dtrPolicy,
                     m_rtsPolicy,
                     m_hostTxEnabled ? QStringLiteral("on") : QStringLiteral("off"),
                     m_controlEnabled ? QStringLiteral("on") : QStringLiteral("off"),
                     m_labGatewayEnabled ? QStringLiteral("on") : QStringLiteral("off")),
            passiveUnsafePolicy),
        row(QStringLiteral("capture_storage"),
            QStringLiteral("Capture storage"),
            captureLevel,
            m_captureActive ? QStringLiteral("recording") : QStringLiteral("idle/finalized"),
            QStringLiteral("bytes %1 records %2").arg(m_captureBytesWritten).arg(m_captureRecordCount),
            false),
        row(QStringLiteral("typed_parser"),
            QStringLiteral("Typed parser"),
            parserLevel,
            QStringLiteral("frames %1 faults %2").arg(m_typedFrames).arg(parserFaults),
            QStringLiteral("drop %1 crc %2 len %3 seq %4 ver %5")
                .arg(m_bytesDropped)
                .arg(m_crcFailures)
                .arg(m_lengthFailures)
                .arg(m_seqGaps)
                .arg(m_versionWarnings),
            parserFaults > 0),
        row(QStringLiteral("host_drain"),
            QStringLiteral("Host drain"),
            drainLevel,
            QStringLiteral("bytes %1 reads %2").arg(m_drainBytesTotal).arg(m_drainReadyReadCount),
            QStringLiteral("ready_max_us %1 burst_max %2 raw_q %3/%4 max %5 overrun %6 contention %7 parse_backlog %8 parser_batch_max_ms %9")
                .arg(m_drainReadyReadMaxUs)
                .arg(m_drainBurstMaxBytes)
                .arg(m_rawQueueUsedBytes)
                .arg(m_rawQueueCapacityBytes)
                .arg(m_rawQueueMaxUsedBytes)
                .arg(m_rawQueueOverrunBytes)
                .arg(m_rawQueueContentionCount)
                .arg(m_parseBacklogBytes)
                .arg(m_parserBatchMaxMs),
            m_rawQueueOverrunBytes > 0),
        row(QStringLiteral("capture_writer"),
            QStringLiteral("Capture writer"),
            m_captureWriterOverrunBytes > 0 ? QStringLiteral("ERR") : QStringLiteral("OK"),
            QStringLiteral("queue %1 max %2").arg(m_captureWriterQueueBytes).arg(m_captureWriterMaxQueueBytes),
            QStringLiteral("overrun %1 write_max_ms %2")
                .arg(m_captureWriterOverrunBytes)
                .arg(m_captureWriteMaxMs),
            m_captureWriterOverrunBytes > 0),
        row(QStringLiteral("analysis_queue"),
            QStringLiteral("Analysis queue"),
            analysisQueueLevel,
            QStringLiteral("frames %1/%2 max %3")
                .arg(m_analysisQueuedFrames)
                .arg(m_analysisCapacityFrames)
                .arg(m_analysisMaxQueuedFrames),
            QStringLiteral("enqueued %1 processed %2 overrun %3 truth_loss %4 pumps %5 pump_max_ms %6 snapshot_max_ms %7")
                .arg(m_analysisEnqueuedFrames)
                .arg(m_analysisProcessedFrames)
                .arg(m_analysisOverrunFrames)
                .arg(m_analysisTruthLoss)
                .arg(m_analysisPumpCount)
                .arg(m_analysisPumpMaxMs)
                .arg(m_analysisSnapshotMaxMs),
            m_analysisOverrunFrames > 0 || m_analysisTruthLoss > 0),
        row(QStringLiteral("host_tx_queue"),
            QStringLiteral("Host TX queue"),
            hostLevel,
            QStringLiteral("queued %1 / %2 bytes").arg(m_hostQueuedFrames).arg(m_hostQueuedBytes),
            QStringLiteral("enqueued %1 written %2 dropped %3")
                .arg(m_hostEnqueuedFrames)
                .arg(m_hostWrittenFrames)
                .arg(m_hostDroppedFrames),
            m_hostDroppedFrames > 0),
        row(QStringLiteral("board_health"),
            QStringLiteral("Board health"),
            boardLevel,
            m_boardHealthAgeMs >= 0 ? QStringLiteral("health age %1ms").arg(m_boardHealthAgeMs) : QStringLiteral("no health yet"),
            QStringLiteral("can_drop %1 fifo_overflow %2").arg(m_boardCanDroppedTotal).arg(m_boardFifoOverflowTotal),
            m_boardCanDroppedTotal > 0 || m_boardFifoOverflowTotal > 0),
        row(QStringLiteral("csm_uplink"),
            QStringLiteral("CSM uplink"),
            uplinkLevel,
            m_boardUplink.present
                ? QStringLiteral("ring_clear %1 backpressure %2")
                      .arg(m_boardUplink.serialRingClearTotal)
                      .arg(m_boardUplink.serialBackpressureTotal)
                : QStringLiteral("extended health counters unavailable"),
            m_boardUplink.present
                ? (m_boardUplink.hasPoolCounters
                       ? QStringLiteral("cleared_bytes %1 enqueue_fail %2 can_segment_enqueue_fail %3 pool_fail %4 can_pool_fail %5 large_pool %6/%7 can_reserve_used %8 can_q_high %9 desc_high %10 diag_suppressed %11")
                             .arg(m_boardUplink.serialRingClearedBytesTotal)
                             .arg(m_boardUplink.serialEnqueueFailTotal)
                             .arg(m_boardUplink.canSegmentEnqueueFailTotal)
                             .arg(m_boardUplink.uplinkPoolAllocFailTotal)
                             .arg(m_boardUplink.canTruthPoolAllocFailTotal)
                             .arg(m_boardUplink.uplinkLargePoolUsedBlocks)
                             .arg(m_boardUplink.uplinkLargePoolCapacityBlocks)
                             .arg(m_boardUplink.uplinkLargePoolCanReserveUsedBlocks)
                             .arg(m_boardUplink.canTruthDescriptorQueueHighWater)
                             .arg(m_boardUplink.uplinkDescriptorHighWaterTotal)
                             .arg(m_boardUplink.diagnosticSuppressedTotal)
                       : QStringLiteral("cleared_bytes %1 enqueue_fail %2 can_segment_enqueue_fail %3 serial_high_water %4 shared_queue_high %5 mcp_budget_hits %6")
                      .arg(m_boardUplink.serialRingClearedBytesTotal)
                      .arg(m_boardUplink.serialEnqueueFailTotal)
                      .arg(m_boardUplink.canSegmentEnqueueFailTotal)
                      .arg(m_boardUplink.serialTxHighWaterBytes)
                      .arg(m_boardUplink.sharedCanQueueHighWater)
                      .arg(m_boardUplink.mcpDrainBudgetHitTotal))
                : QStringLiteral("BOARD_HEALTH payload is legacy 52B; cannot separate USB backpressure from board CAN counters"),
            m_boardUplink.present &&
                (m_boardUplink.serialRingClearTotal > 0 ||
                 m_boardUplink.serialRingClearedBytesTotal > 0 ||
                 m_boardUplink.canSegmentEnqueueFailTotal > 0 ||
                 m_boardUplink.canTruthPoolAllocFailTotal > 0)),
        row(QStringLiteral("board_events"),
            QStringLiteral("Board events"),
            eventLevel,
            m_mcp2515EventTotal > 0
                ? QStringLiteral("MCP2515 %1 last 0x%2")
                      .arg(m_mcp2515EventTotal)
                      .arg(m_lastBoardEventDetail, 4, 16, QLatin1Char('0')).toUpper()
                : QStringLiteral("events %1").arg(m_boardEventTotal),
            boardEventDetailText(),
            m_boardEventFatalTotal > 0),
        row(QStringLiteral("live_latest"),
            QStringLiteral("Live latest view"),
            latestLevel,
            QStringLiteral("observed %1 emitted %2 pending %3")
                .arg(m_liveLatestObservedCanRxFrames)
                .arg(m_liveLatestEmittedFrames)
                .arg(m_liveLatestPendingKeys),
            QStringLiteral("bus0 %1 bus1 %2 coalesced_snapshot_updates %3 flushes %4 max_pending %5 last_in %6 last_out %7 flush_ms %8 display_loss %9")
                .arg(m_liveLatestObservedBus0CanRxFrames)
                .arg(m_liveLatestObservedBus1CanRxFrames)
                .arg(m_liveLatestCoalescedUpdates)
                .arg(m_liveLatestFlushCount)
                .arg(m_liveLatestMaxPendingKeys)
                .arg(m_liveLatestLastInputRecords)
                .arg(m_liveLatestLastOutputFrames)
                .arg(m_liveLatestLastFlushMs)
                .arg(m_displayLoss),
            m_displayLoss > 0),
        row(QStringLiteral("decoded_can_tail"),
            QStringLiteral("Decoded CAN tail"),
            rawLedgerLevel,
            QStringLiteral("rows %1 visible %2 latest %3")
                .arg(m_rawLedgerTotalRows)
                .arg(m_rawLedgerVisibleRows)
                .arg(m_rawLedgerLatestSeq),
            QStringLiteral("segment_bytes %1 display_dropped %2 derived view; authoritative truth capture.stream/index")
                .arg(m_rawLedgerSegmentBytes)
                .arg(m_rawLedgerDroppedDisplayRows),
            false),
        row(QStringLiteral("live_projection"),
            QStringLiteral("Live projection"),
            projectionLevel,
            QStringLiteral("pending %1 projected %2").arg(m_pendingLiveFrames).arg(m_projectedFrames),
            QStringLiteral("sampled %1 view %2 dropped %3 control %4/%5 sampled %6 max_backlog %7 budget_hits %8 last_flush_ms %9")
                .arg(m_sampledProjectionFrames)
                .arg(m_sampledViewDrops)
                .arg(m_droppedProjectionFrames)
                .arg(m_projectedControlEvidenceRecords)
                .arg(m_observedControlEvidenceRecords)
                .arg(m_sampledControlEvidenceRecords)
                .arg(m_maxProjectionBacklog)
                .arg(m_flushBudgetHits)
                .arg(m_lastFlushMs),
            false),
        row(QStringLiteral("live_delay"),
            QStringLiteral("Live delay"),
            liveLevel(),
            liveStateText(),
            QStringLiteral("frame_age %1 stats_age %2").arg(m_liveFrameAgeMs).arg(m_liveStatsAgeMs),
            false),
        row(QStringLiteral("live_path_trace"),
            QStringLiteral("Live path trace"),
            liveTraceRowLevel,
            QStringLiteral("parsed %1 snapshot %2/%3f serial_emit %4/%5f app_recv %6 pending %7 model %8")
                .arg(traceText(m_livePathTrace, QStringLiteral("parsed_can_rx")))
                .arg(traceText(m_livePathTrace, QStringLiteral("snapshot_emitted")))
                .arg(traceText(m_livePathTrace, QStringLiteral("snapshot_emitted_frames")))
                .arg(traceText(m_livePathTrace, QStringLiteral("frames_received_emit")))
                .arg(traceText(m_livePathTrace, QStringLiteral("frames_received_frames")))
                .arg(traceText(m_livePathTrace, QStringLiteral("framesReceived_calls")))
                .arg(traceText(m_livePathTrace, QStringLiteral("pending_live_rows")))
                .arg(traceText(m_livePathTrace, QStringLiteral("live_model_rows"))),
            QStringLiteral("ack %1 inflight %2 coalesced %3 dropped %4 projected_rx %5 queue_keys %6 flush %7 app_frames %8 view_q %9 view_flush %10 append %11/%12 paused_drop %13 panel_drop %14 timers live:%15/%16 view:%17/%18 seq %19/%20 delay %21/%22ms snap %23/%24/%25 rows %26")
                .arg(traceText(m_livePathTrace, QStringLiteral("snapshot_ack")))
                .arg(traceText(m_livePathTrace, QStringLiteral("snapshot_inflight"), QStringLiteral("false")))
                .arg(traceText(m_livePathTrace, QStringLiteral("snapshot_coalesced")))
                .arg(traceText(m_livePathTrace, QStringLiteral("snapshot_dropped")))
                .arg(traceText(m_livePathTrace, QStringLiteral("projected_frames_received")))
                .arg(traceText(m_livePathTrace, QStringLiteral("projection_queue_keys")))
                .arg(traceText(m_livePathTrace, QStringLiteral("projection_flush_count")))
                .arg(traceText(m_livePathTrace, QStringLiteral("framesReceived_frames")))
                .arg(traceText(m_livePathTrace, QStringLiteral("queue_live_view_frames")))
                .arg(traceText(m_livePathTrace, QStringLiteral("live_view_flush_calls")))
                .arg(traceText(m_livePathTrace, QStringLiteral("append_live_batch_calls")))
                .arg(traceText(m_livePathTrace, QStringLiteral("append_live_batch_frames")))
                .arg(traceText(m_livePathTrace, QStringLiteral("live_view_paused_drops")))
                .arg(traceText(m_livePathTrace, QStringLiteral("live_view_panel_drops")))
                .arg(traceText(m_livePathTrace, QStringLiteral("liveFlushTimerActive"), QStringLiteral("false")))
                .arg(traceText(m_livePathTrace, QStringLiteral("live_flush_timer_fire_count")))
                .arg(traceText(m_livePathTrace, QStringLiteral("liveViewFlushTimerActive"), QStringLiteral("false")))
                .arg(traceText(m_livePathTrace, QStringLiteral("live_view_flush_timer_fire_count")))
                .arg(traceText(m_livePathTrace, QStringLiteral("frames_received_emit_seq")))
                .arg(traceText(m_livePathTrace, QStringLiteral("framesReceived_slot_seq")))
                .arg(traceText(m_livePathTrace, QStringLiteral("framesReceived_slot_delay_ms"), QStringLiteral("-1")))
                .arg(traceText(m_livePathTrace, QStringLiteral("framesReceived_slot_delay_max_ms")))
                .arg(traceText(m_livePathTrace, QStringLiteral("app_snapshot_receive")))
                .arg(traceText(m_livePathTrace, QStringLiteral("app_snapshot_ack")))
                .arg(traceText(m_livePathTrace, QStringLiteral("app_snapshot_apply")))
                .arg(traceText(m_livePathTrace, QStringLiteral("app_snapshot_apply_rows"))),
            liveTraceRowLevel == QStringLiteral("ERR")),
        row(QStringLiteral("drain_event_trace"),
            QStringLiteral("Drain event trace"),
            drainTraceRowLevel,
            QStringLiteral("readyRead/s %1 emit/s %2 schedule/s %3 pump/s %4 outSig/s %5")
                .arg(traceText(m_drainEventTrace, QStringLiteral("readyRead_per_sec")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("bytes_available_emit_per_sec")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("scheduleDrainPump_per_sec")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("pump_per_sec")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("output_signal_per_sec"))),
            QStringLiteral("ready %1 emit %2 schedule %3 invoke %4 suppressed %5 pump %6 ignored %7 blocks %8 bytes %9 status %10 diag %11 rawQ %12/%13 max %14 overrun %15 statusRx proj/latest/typed %16/%17/%18 app %19/%20/%21 analysis pend %22 inflight %23 done %24/%25 over %26 raw pend %27/%28B inflight %29 done %30/%31 overB %32 latest emit %33/%34 pend %35")
                .arg(traceText(m_drainEventTrace, QStringLiteral("readyRead_calls")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("bytes_available_emits")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("scheduleDrainPump_calls")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("typed_worker_invoke_requests")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("typed_worker_invoke_suppressed")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("pump_calls")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("schedulePump_ignored_already_scheduled")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("pump_blocks")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("pump_bytes")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("status_signal_count")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("diagnostics_signal_count")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("drain_queue_used_bytes")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("drain_queue_capacity_bytes")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("drain_queue_max_used_bytes")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("drain_queue_overrun_bytes")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("typedProjectionStatus_receive")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("typedLiveLatestStatus_receive")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("typedTransportStatus_receive")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("app_typedProjectionStatus_receive")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("app_typedLiveLatestStatus_receive")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("app_typedTransportStatus_receive")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("analysis_handoff_pending_frames")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("analysis_handoff_inflight"), QStringLiteral("false")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("analysis_handoff_complete_count")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("analysis_handoff_complete_frames")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("analysis_handoff_overrun_frames")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("raw_ledger_handoff_pending_frames")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("raw_ledger_handoff_pending_bytes")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("raw_ledger_handoff_inflight"), QStringLiteral("false")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("raw_ledger_handoff_complete_count")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("raw_ledger_handoff_complete_frames")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("raw_ledger_handoff_overrun_bytes")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("latest_handoff_emit_count")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("latest_handoff_emit_frames")))
                .arg(traceText(m_drainEventTrace, QStringLiteral("latest_handoff_pending_keys"))),
            drainTraceRowLevel == QStringLiteral("ERR"))
    };
}

} // namespace CanMonitorTransport
