#include "transport/CaptureCoreRuntime.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cstring>

namespace {
constexpr int kCoreLocalBatchSize = 128;
constexpr int kCoreLiveLatestMaxKeys = 256;

CanMonitorCore::CoreViewSeverity maxSeverity(CanMonitorCore::CoreViewSeverity lhs,
                                             CanMonitorCore::CoreViewSeverity rhs) {
    return static_cast<int>(lhs) >= static_cast<int>(rhs) ? lhs : rhs;
}

QString u64Text(quint64 value) {
    return QString::number(value);
}

QJsonArray boolPairToJson(const bool values[2]) {
    return QJsonArray{values[0], values[1]};
}

QJsonObject boardHealthToJson(const TypedBoardHealthRecord& health) {
    QJsonObject out;
    out.insert(QStringLiteral("mono_us"), u64Text(health.monoUs));
    out.insert(QStringLiteral("can_rx_total"), u64Text(health.canRxTotal));
    out.insert(QStringLiteral("can_dropped_total"), u64Text(health.canDroppedTotal));
    out.insert(QStringLiteral("can_fifo_overflow_total"), u64Text(health.canFifoOverflowTotal));
    out.insert(QStringLiteral("serial_record_tx_total"), u64Text(health.serialRecordTxTotal));
    out.insert(QStringLiteral("queue_depth"), int(health.queueDepth));
    out.insert(QStringLiteral("encoder_fault_events"), u64Text(health.encoderFaultEvents));
    out.insert(QStringLiteral("encoder_wrap_events"), u64Text(health.encoderWrapEvents));
    out.insert(QStringLiteral("encoder_position"), QString::number(health.encoderPosition));
    out.insert(QStringLiteral("safety_state"), int(health.safetyState));
    out.insert(QStringLiteral("inputs"), int(health.inputs));
    out.insert(QStringLiteral("encoder_timer_ok"), int(health.encoderTimerOk));
    out.insert(QStringLiteral("flags"), int(health.flags));
    out.insert(QStringLiteral("fault_flags"), u64Text(health.faultFlags));
    out.insert(QStringLiteral("has_extended_transport_counters"), health.hasExtendedTransportCounters);
    out.insert(QStringLiteral("serial_enqueue_fail_total"), u64Text(health.serialEnqueueFailTotal));
    out.insert(QStringLiteral("serial_ring_clear_total"), u64Text(health.serialRingClearTotal));
    out.insert(QStringLiteral("serial_ring_cleared_bytes_total"), u64Text(health.serialRingClearedBytesTotal));
    out.insert(QStringLiteral("serial_backpressure_total"), u64Text(health.serialBackpressureTotal));
    out.insert(QStringLiteral("serial_tx_high_water_bytes"), u64Text(health.serialTxHighWaterBytes));
    out.insert(QStringLiteral("shared_can_queue_high_water"), u64Text(health.sharedCanQueueHighWater));
    out.insert(QStringLiteral("mcp_drain_budget_hit_total"), u64Text(health.mcpDrainBudgetHitTotal));
    out.insert(QStringLiteral("can_segment_enqueue_fail_total"), u64Text(health.canSegmentEnqueueFailTotal));
    out.insert(QStringLiteral("has_uplink_pool_counters"), health.hasUplinkPoolCounters);
    out.insert(QStringLiteral("uplink_large_pool_used_blocks"), u64Text(health.uplinkLargePoolUsedBlocks));
    out.insert(QStringLiteral("uplink_large_pool_capacity_blocks"), u64Text(health.uplinkLargePoolCapacityBlocks));
    out.insert(QStringLiteral("uplink_large_pool_can_reserve_used_blocks"), u64Text(health.uplinkLargePoolCanReserveUsedBlocks));
    out.insert(QStringLiteral("can_truth_descriptor_queue_high_water"), u64Text(health.canTruthDescriptorQueueHighWater));
    out.insert(QStringLiteral("uplink_pool_alloc_fail_total"), u64Text(health.uplinkPoolAllocFailTotal));
    out.insert(QStringLiteral("can_truth_pool_alloc_fail_total"), u64Text(health.canTruthPoolAllocFailTotal));
    out.insert(QStringLiteral("uplink_descriptor_high_water_total"), u64Text(health.uplinkDescriptorHighWaterTotal));
    out.insert(QStringLiteral("diagnostic_suppressed_total"), u64Text(health.diagnosticSuppressedTotal));
    out.insert(QStringLiteral("has_passive_lifecycle_counters"), health.hasPassiveLifecycleCounters);
    out.insert(QStringLiteral("host_absent_rx_discard_bus0_total"), u64Text(health.hostAbsentRxDiscardBus0Total));
    out.insert(QStringLiteral("host_absent_rx_discard_bus1_total"), u64Text(health.hostAbsentRxDiscardBus1Total));
    out.insert(QStringLiteral("host_absent_fifo_overflow_total"), u64Text(health.hostAbsentFifoOverflowTotal));
    out.insert(QStringLiteral("host_absent_mcp_error_total"), u64Text(health.hostAbsentMcpErrorTotal));
    out.insert(QStringLiteral("host_absent_duration_ms_total"), u64Text(health.hostAbsentDurationMsTotal));
    out.insert(QStringLiteral("passive_readback_total"), u64Text(health.passiveReadbackTotal));
    out.insert(QStringLiteral("passive_readback_violation_total"), u64Text(health.passiveReadbackViolationTotal));
    out.insert(QStringLiteral("txreq_violation_total"), u64Text(health.txreqViolationTotal));
    out.insert(QStringLiteral("usb_cdc_dtr_change_total"), u64Text(health.usbCdcDtrChangeTotal));
    return out;
}

QJsonObject capabilityToJson(const TypedCapabilityRecord& capability) {
    QJsonObject out;
    out.insert(QStringLiteral("mono_us"), u64Text(capability.monoUs));
    out.insert(QStringLiteral("protocol_version"), int(capability.protocolVersion));
    out.insert(QStringLiteral("profile_major"), int(capability.profileMajor));
    out.insert(QStringLiteral("profile_minor"), int(capability.profileMinor));
    out.insert(QStringLiteral("mono_unit"), int(capability.monoUnit));
    out.insert(QStringLiteral("can_queue_size"), u64Text(capability.canQueueSize));
    out.insert(QStringLiteral("encoder_ppr"), u64Text(capability.encoderPpr));
    out.insert(QStringLiteral("encoder_frequency_limit"), u64Text(capability.encoderFrequencyLimit));
    out.insert(QStringLiteral("supports_can_rx_raw"), capability.supportsCanRxRaw);
    out.insert(QStringLiteral("supports_can_tx_raw"), capability.supportsCanTxRaw);
    out.insert(QStringLiteral("supports_enc_edge_raw"), capability.supportsEncEdgeRaw);
    out.insert(QStringLiteral("supports_enc_derived"), capability.supportsEncDerived);
    out.insert(QStringLiteral("supports_adc_sample"), capability.supportsAdcSample);
    out.insert(QStringLiteral("supports_board_health"), capability.supportsBoardHealth);
    out.insert(QStringLiteral("supports_board_event"), capability.supportsBoardEvent);
    out.insert(QStringLiteral("adc_channels"), int(capability.adcChannels));
    out.insert(QStringLiteral("adc_resolution_bits"), int(capability.adcResolutionBits));
    out.insert(QStringLiteral("adc_period_ms"), int(capability.adcPeriodMs));
    out.insert(QStringLiteral("lane_capability_flags"), int(capability.laneCapabilityFlags));
    out.insert(QStringLiteral("limitation_flags"), int(capability.limitationFlags));
    out.insert(QStringLiteral("bus_count"), int(capability.busCount));
    out.insert(QStringLiteral("bus_descriptor_size"), int(capability.busDescriptorSize));
    out.insert(QStringLiteral("capability_v2_flags"), int(capability.capabilityV2Flags));
    out.insert(QStringLiteral("supported_uplink_records"), u64Text(capability.supportedUplinkRecords));
    out.insert(QStringLiteral("supported_downlink_records"), u64Text(capability.supportedDownlinkRecords));
    out.insert(QStringLiteral("safety_feature_flags"), u64Text(capability.safetyFeatureFlags));
    out.insert(QStringLiteral("policy_hash"), u64Text(capability.policyHash));
    out.insert(QStringLiteral("firmware_build_id"), u64Text(capability.firmwareBuildId));
    out.insert(QStringLiteral("host_tx_queue_size"), int(capability.hostTxQueueSize));
    out.insert(QStringLiteral("capability_v3_flags"), int(capability.capabilityV3Flags));
    out.insert(QStringLiteral("has_firmware_identity"), capability.hasFirmwareIdentity);
    out.insert(QStringLiteral("firmware_identity_version"), int(capability.firmwareIdentityVersion));
    out.insert(QStringLiteral("firmware_dirty"), capability.firmwareDirty);
    out.insert(QStringLiteral("firmware_irq_mode"), int(capability.firmwareIrqMode));
    out.insert(QStringLiteral("firmware_build_epoch"), u64Text(capability.firmwareBuildEpoch));
    out.insert(QStringLiteral("mcp_spi_hz"), u64Text(capability.mcpSpiHz));
    out.insert(QStringLiteral("can_record_drain_budget"), int(capability.canRecordDrainBudget));
    out.insert(QStringLiteral("serial_ring_kib"), int(capability.serialRingKiB));
    out.insert(QStringLiteral("firmware_git_sha"), capability.firmwareGitSha);
    out.insert(QStringLiteral("firmware_env_name"), capability.firmwareEnvName);
    out.insert(QStringLiteral("has_passive_policy"), capability.hasPassivePolicy);
    out.insert(QStringLiteral("firmware_profile"), int(capability.firmwareProfile));
    out.insert(QStringLiteral("profile_lock_state"), int(capability.profileLockState));
    out.insert(QStringLiteral("capability_vehicle_impact_state"), int(capability.vehicleImpactState));
    out.insert(QStringLiteral("host_command_rx"), capability.hostCommandRx);
    out.insert(QStringLiteral("control_path"), capability.controlPath);
    out.insert(QStringLiteral("usb_backpressure_isolated"), capability.usbBackpressureIsolated);
    out.insert(QStringLiteral("dtr_reset_sensitive"), capability.dtrResetSensitive);
    out.insert(QStringLiteral("passive_acceptance_allowed"), capability.passiveAcceptanceAllowed);
    out.insert(QStringLiteral("hardware_safety_case_id"), u64Text(capability.hardwareSafetyCaseId));
    out.insert(QStringLiteral("bench_verification_id"), u64Text(capability.benchVerificationId));
    out.insert(QStringLiteral("usb_cdc_dtr_session_required"), capability.usbCdcDtrSessionRequired);
    out.insert(QStringLiteral("usb_cdc_dtr_session_only"), capability.usbCdcDtrSessionOnly);
    out.insert(QStringLiteral("has_passive_hardware_evidence_claims"), capability.hasPassiveHardwareEvidenceClaims);
    out.insert(QStringLiteral("passive_hardware_evidence_schema"), int(capability.passiveHardwareEvidenceSchema));
    out.insert(QStringLiteral("hardware_silent_strapped"), boolPairToJson(capability.hardwareSilentStrapped));
    out.insert(QStringLiteral("galvanic_isolated"), boolPairToJson(capability.galvanicIsolated));
    out.insert(QStringLiteral("power_off_passive"), boolPairToJson(capability.powerOffPassive));
    out.insert(QStringLiteral("reset_safe"), boolPairToJson(capability.resetSafe));
    out.insert(QStringLiteral("txd_gated"), boolPairToJson(capability.txdGated));
    out.insert(QStringLiteral("normal_enable_path_populated"), boolPairToJson(capability.normalEnablePathPopulated));
    out.insert(QStringLiteral("field_sku_id"), u64Text(capability.fieldSkuId));
    out.insert(QStringLiteral("external_analyzer_artifact_id"), u64Text(capability.externalAnalyzerArtifactId));
    out.insert(QStringLiteral("hotplug_pass_count"), u64Text(capability.hotplugPassCount));
    out.insert(QStringLiteral("host_session_epoch"), u64Text(capability.hostSessionEpoch));
    out.insert(QStringLiteral("transport_epoch"), u64Text(capability.transportEpoch));
    out.insert(QStringLiteral("usb_attach_quarantine_total"), u64Text(capability.usbAttachQuarantineTotal));
    out.insert(QStringLiteral("host_absent_gap_total"), u64Text(capability.hostAbsentGapTotal));
    out.insert(QStringLiteral("pre_session_payload_replay_total"), u64Text(capability.preSessionPayloadReplayTotal));
    QJsonArray buses;
    for (const TypedCapabilityBusDescriptor& bus : capability.buses) {
        buses.append(QJsonObject{{QStringLiteral("bus_id"), int(bus.busId)},
                                 {QStringLiteral("role_hint"), int(bus.roleHint)},
                                 {QStringLiteral("backend"), int(bus.backend)},
                                 {QStringLiteral("transceiver"), int(bus.transceiver)},
                                 {QStringLiteral("rx_supported"), bus.rxSupported},
                                 {QStringLiteral("tx_supported"), bus.txSupported},
                                 {QStringLiteral("control_tx_allowed"), bus.controlTxAllowed},
                                 {QStringLiteral("classic_can_supported"), bus.classicCanSupported},
                                 {QStringLiteral("can_fd_supported"), bus.canFdSupported},
                                 {QStringLiteral("max_live_dlc"), int(bus.maxLiveDlc)},
                                 {QStringLiteral("nominal_bitrate"), u64Text(bus.nominalBitrate)},
                                 {QStringLiteral("data_bitrate"), u64Text(bus.dataBitrate)},
                                 {QStringLiteral("termination_policy"), int(bus.terminationPolicy)},
                                 {QStringLiteral("isolation_policy"), int(bus.isolationPolicy)}});
    }
    out.insert(QStringLiteral("buses"), buses);
    return out;
}

QJsonObject boardEventToJson(const TypedBoardEventRecord& event) {
    const auto eventName = [](quint16 code) {
        switch (code) {
        case 1: return QStringLiteral("BOOT");
        case 9: return QStringLiteral("MCP2515_ERROR");
        case 10: return QStringLiteral("MCP2515_SPI_SNAPSHOT");
        case 23: return QStringLiteral("FIRMWARE_IDENTITY");
        case 24: return QStringLiteral("SERIAL_TX_BACKPRESSURE");
        case 25: return QStringLiteral("SERIAL_TX_RING_CLEAR");
        case 26: return QStringLiteral("CAN_RX_SEGMENT_ENQUEUE_FAILED");
        case 27: return QStringLiteral("USB_CDC_SESSION_OPEN");
        case 28: return QStringLiteral("USB_CDC_SESSION_CLOSE");
        case 29: return QStringLiteral("USB_CDC_DTR_CHANGE");
        case 30: return QStringLiteral("USB_HOST_ABSENT_CAN_DISCARD_SUMMARY");
        case 31: return QStringLiteral("MCP_PASSIVE_MODE_READBACK");
        case 32: return QStringLiteral("MCP_PASSIVE_MODE_VIOLATION");
        case 33: return QStringLiteral("MCP_TXREQ_VIOLATION");
        case 34: return QStringLiteral("TRANSCEIVER_SAFE_STATE_CHANGED");
        case 35: return QStringLiteral("USB_POWER_OR_RESET_SUSPECTED");
        case 36: return QStringLiteral("CAN_FRONTEND_PRESESSION_HOLD");
        case 37: return QStringLiteral("CAN_FRONTEND_SESSION_READY");
        case 38: return QStringLiteral("CAN_FRONTEND_SESSION_INIT_FAILED");
        case 39: return QStringLiteral("CAN_FRONTEND_FAULT_HOLD");
        default: return QStringLiteral("BOARD_EVENT_%1").arg(code);
        }
    };
    return QJsonObject{{QStringLiteral("mono_us"), u64Text(event.monoUs)},
                       {QStringLiteral("code"), int(event.code)},
                       {QStringLiteral("name"), eventName(event.code)},
                       {QStringLiteral("detail"), int(event.detail)},
                       {QStringLiteral("counter"), u64Text(event.counter)}};
}

QJsonObject mcpDetailsToJson(const QHash<quint16, quint64>& details) {
    QJsonObject out;
    for (auto it = details.cbegin(); it != details.cend(); ++it) {
        out.insert(QStringLiteral("0x%1").arg(it.key(), 4, 16, QLatin1Char('0')).toUpper(), u64Text(it.value()));
    }
    return out;
}

CanMonitorTransport::CanRxLite canRxLiteFromCanRawRecord(const TypedRecord& record, const TypedCanRawRecord& can) {
    CanMonitorTransport::CanRxLite frame;
    frame.monoUs = can.monoUs;
    frame.canId = can.canId;
    frame.extended = can.extended;
    frame.rtr = can.rtr;
    frame.dlc = can.dlc;
    frame.bus = can.bus;
    frame.typedSeqLsb = quint8(record.header.seq & 0xFF);
    std::memcpy(frame.data, can.data, sizeof(frame.data));
    return frame;
}

CanMonitorTransport::CanRxLite canRxLiteFromSegmentEntryRecord(const TypedRecord& record, const TypedCanRxSegmentEntry& entry) {
    CanMonitorTransport::CanRxLite frame;
    frame.monoUs = entry.monoUs;
    frame.canId = entry.canId;
    frame.extended = entry.extended;
    frame.rtr = entry.rtr;
    frame.dlc = entry.dlc;
    frame.bus = entry.bus;
    frame.typedSeqLsb = quint8(record.header.seq & 0xFF);
    frame.hasCaptureSeq = true;
    frame.captureSeq = entry.captureSeq;
    std::memcpy(frame.data, entry.data, sizeof(frame.data));
    return frame;
}

QString frameDataHex(const CanMonitorTransport::CanRxLite& frame) {
    QByteArray bytes;
    bytes.reserve(8);
    for (quint8 byte : frame.data) {
        bytes.append(char(byte));
    }
    return QString::fromLatin1(bytes.toHex());
}

QJsonObject frameToViewRow(const CanMonitorTransport::CanRxLite& frame) {
    QJsonObject row;
    row.insert(QStringLiteral("mono_us"), QString::number(frame.monoUs));
    row.insert(QStringLiteral("bus"), int(frame.bus));
    row.insert(QStringLiteral("can_id"), int(frame.canId));
    row.insert(QStringLiteral("ext"), frame.extended);
    row.insert(QStringLiteral("rtr"), frame.rtr);
    row.insert(QStringLiteral("dlc"), int(frame.dlc));
    row.insert(QStringLiteral("data_hex"), frameDataHex(frame));
    row.insert(QStringLiteral("seq"), int(frame.typedSeqLsb));
    if (frame.hasCaptureSeq) {
        row.insert(QStringLiteral("capture_seq"), QString::number(frame.captureSeq));
    }
    return row;
}

CanMonitorCore::CaptureSeqRange captureRangeForFrames(const QVector<CanMonitorTransport::CanRxLite>& frames) {
    CanMonitorCore::CaptureSeqRange range;
    for (const CanMonitorTransport::CanRxLite& frame : frames) {
        if (!frame.hasCaptureSeq) continue;
        if (!range.valid) {
            range.valid = true;
            range.first = frame.captureSeq;
            range.last = frame.captureSeq;
        } else {
            range.first = std::min(range.first, frame.captureSeq);
            range.last = std::max(range.last, frame.captureSeq);
        }
    }
    return range;
}
} // namespace

namespace CanMonitorTransport {

CaptureCoreRuntime::CaptureCoreRuntime(QSharedPointer<TypedRecordHandoffQueue> captureQueue)
    : m_captureQueue(std::move(captureQueue)) {}

void CaptureCoreRuntime::setCaptureQueue(QSharedPointer<TypedRecordHandoffQueue> queue) {
    m_captureQueue = std::move(queue);
}

void CaptureCoreRuntime::setOptions(const Options& options) {
    m_options = options;
}

void CaptureCoreRuntime::reset() {
    m_pipeline.reset();
    m_liveProjection.reset();
    m_liveLatest.reset();
    m_viewStore.clear();
    m_liveLatestByKey.clear();
    m_coreEvidenceTransportPayload = {};
    m_coreEvidenceCheapCounts = {};
    m_coreEvidenceSeverity = CanMonitorCore::CoreViewSeverity::Ok;
    m_boardEventTotal = 0;
    m_mcp2515EventTotal = 0;
    m_boardEventFatalTotal = 0;
    m_mcp2515Details.clear();
    m_liveLatestDropped = 0;
}

CaptureCoreRuntime::Result CaptureCoreRuntime::ingestBlocks(const QVector<DrainByteQueue::Block>& blocks,
                                                            qint64 handshakeElapsedMs,
                                                            quint64 parseBacklogBytes) {
    Result out;
    QVector<CanRxLite> liveLatestFrames;
    TypedCaptureFrameList captureBatch;
    captureBatch.reserve(kCoreLocalBatchSize);
    auto flushCaptureBatch = [this, &captureBatch, &out]() {
        if (captureBatch.isEmpty()) return;
        pushCaptureBatch(std::move(captureBatch), out);
        captureBatch.clear();
        captureBatch.reserve(kCoreLocalBatchSize);
    };

    auto result = m_pipeline.ingestBlocksEach(blocks,
                                              handshakeElapsedMs,
                                              parseBacklogBytes,
                                              m_options.captureRecords,
                                              [this, &captureBatch, &flushCaptureBatch, &liveLatestFrames, &out](TypedRecord&& record) {
                                                  ingestRecordForViews(record, liveLatestFrames, out);
                                                  if (m_options.captureRecords && m_captureQueue) {
                                                      const quint64 monoUs = typedRecordMonoUs(record);
                                                      captureBatch.push_back(TypedCaptureFrame{
                                                          record.header,
                                                          std::move(record.frameBytes),
                                                          monoUs});
                                                      if (captureBatch.size() >= kCoreLocalBatchSize) flushCaptureBatch();
                                                  }
                                              });
    flushCaptureBatch();
    updateLiveLatestView(liveLatestFrames, out);
    out.capabilityFirstSeen = result.capabilityFirstSeen;
    out.capabilityElapsedMs = result.capabilityElapsedMs;
    out.capabilityBytes = result.capabilityBytes;
    out.errors += result.errors;
    out.typedStatusDue = result.statusDue;
    out.typedStatus = result.status;
    updateStatusViews(out, out);
    return out;
}

void CaptureCoreRuntime::ingestRecordForViews(const TypedRecord& record,
                                              QVector<CanRxLite>& liveLatestFrames,
                                              Result& result) {
    if (record.isType(TypedRecordType::CanRxSegment)) {
        QString segmentError;
        const auto segmentHeader = decodeTypedCanRxSegmentHeader(record, &segmentError);
        if (!segmentHeader) {
            result.errors.push_back(QStringLiteral("CAN_RX_SEGMENT rejected: %1").arg(segmentError));
            return;
        }
        for (qsizetype index = 0; index < segmentHeader->frameCount; ++index) {
            if (!decodeTypedCanRxSegmentEntry(record, index, &segmentError)) {
                result.errors.push_back(
                    QStringLiteral("CAN_RX_SEGMENT rejected at entry %1: %2")
                        .arg(index)
                        .arg(segmentError));
                return;
            }
        }
    }

    if (m_options.emitCanRxFrames) {
        const qsizetype before = result.analysisFrames.frames.size();
        appendCanRxFrames(record, result.analysisFrames.frames);
        for (qsizetype i = before; i < result.analysisFrames.frames.size(); ++i) {
            result.rawLedgerFrames.frames.push_back(result.analysisFrames.frames.at(i));
        }
    }

    const auto projection = m_liveProjection.ingestRecord(record);
    if (!projection.criticalRecords.isEmpty()) {
        for (const TypedRecord& criticalRecord : projection.criticalRecords) {
            ingestCriticalRecord(criticalRecord, result);
        }
    }
    liveLatestFrames += projection.projectedFrames;
    if (projection.statusDue) {
        result.projectionStatusDue = true;
        result.projectionStatus = projection.status;
    }

    if (m_options.consumeTruth) {
        const auto truth = m_liveLatest.ingestRecord(record);
        if (truth.statusDue) {
            result.latestStatusDue = true;
            result.latestStatus = truth.status;
        }
    }
}

void CaptureCoreRuntime::pushCaptureBatch(TypedCaptureFrameList&& batch, Result& result) {
    if (batch.isEmpty() || !m_options.captureRecords || !m_captureQueue) return;

    auto push = m_captureQueue->push(std::move(batch));
    result.captureDrainNeeded = result.captureDrainNeeded || push.shouldScheduleDrain;
    if (!push.accepted && push.records > 0) {
        result.captureHandoffOverrun = true;
        result.captureHandoffError = push.error;
        result.captureHandoffOverrunRecords += push.records;
        result.captureHandoffOverrunBytes += push.bytes;
    }
}

void CaptureCoreRuntime::ingestCriticalRecord(const TypedRecord& record, Result& result) {
    QJsonObject payload = m_coreEvidenceTransportPayload;
    QJsonObject counts = m_coreEvidenceCheapCounts;
    auto severity = m_coreEvidenceSeverity;

    const TypedRecordType type = record.header.type();
    if (type == TypedRecordType::Capability) {
        const auto capability = decodeTypedCapability(record);
        if (!capability) return;
        payload.insert(QStringLiteral("capability"), capabilityToJson(*capability));
        payload.insert(QStringLiteral("capability_seen"), true);
        payload.insert(QStringLiteral("capability_mono_us"), u64Text(capability->monoUs));
        counts.insert(QStringLiteral("capability_seen"), true);
    } else if (type == TypedRecordType::BoardHealth) {
        const auto health = decodeTypedBoardHealth(record);
        if (!health) return;
        payload.insert(QStringLiteral("board_health"), boardHealthToJson(*health));
        payload.insert(QStringLiteral("board_health_seen"), true);
        payload.insert(QStringLiteral("board_health_mono_us"), u64Text(health->monoUs));
        counts.insert(QStringLiteral("board_health_seen"), true);
        counts.insert(QStringLiteral("board_can_dropped_total"), u64Text(health->canDroppedTotal));
        counts.insert(QStringLiteral("board_fifo_overflow_total"), u64Text(health->canFifoOverflowTotal));
        if (health->canDroppedTotal > 0 ||
            health->canFifoOverflowTotal > 0 ||
            health->serialRingClearTotal > 0 ||
            health->canSegmentEnqueueFailTotal > 0 ||
            health->canTruthPoolAllocFailTotal > 0 ||
            health->passiveReadbackViolationTotal > 0 ||
            health->txreqViolationTotal > 0) {
            severity = maxSeverity(severity, CanMonitorCore::CoreViewSeverity::Error);
        } else if (health->serialEnqueueFailTotal > 0 ||
                   health->serialBackpressureTotal > 0 ||
                   health->mcpDrainBudgetHitTotal > 0 ||
                   health->uplinkPoolAllocFailTotal > 0 ||
                   health->hostAbsentFifoOverflowTotal > 0 ||
                   health->hostAbsentMcpErrorTotal > 0) {
            severity = maxSeverity(severity, CanMonitorCore::CoreViewSeverity::Warn);
        }
    } else if (type == TypedRecordType::BoardEvent) {
        const auto event = decodeTypedBoardEvent(record);
        if (!event) return;
        ++m_boardEventTotal;
        if (event->code == 9) {
            ++m_mcp2515EventTotal;
            m_mcp2515Details[event->detail] = m_mcp2515Details.value(event->detail) + 1;
            severity = maxSeverity(severity, CanMonitorCore::CoreViewSeverity::Warn);
        }
        if (event->code == 12 || event->code == 17 || event->code == 32 || event->code == 33 || event->code == 35) {
            ++m_boardEventFatalTotal;
            severity = maxSeverity(severity, CanMonitorCore::CoreViewSeverity::Error);
        }
        payload.insert(QStringLiteral("last_board_event"), boardEventToJson(*event));
        payload.insert(QStringLiteral("board_event_total"), u64Text(m_boardEventTotal));
        payload.insert(QStringLiteral("mcp2515_event_total"), u64Text(m_mcp2515EventTotal));
        payload.insert(QStringLiteral("board_event_fatal_total"), u64Text(m_boardEventFatalTotal));
        payload.insert(QStringLiteral("mcp2515_details"), mcpDetailsToJson(m_mcp2515Details));
        counts.insert(QStringLiteral("board_event_total"), u64Text(m_boardEventTotal));
        counts.insert(QStringLiteral("mcp2515_event_total"), u64Text(m_mcp2515EventTotal));
        counts.insert(QStringLiteral("board_event_fatal_total"), u64Text(m_boardEventFatalTotal));
    } else if (type == TypedRecordType::ControlAck) {
        const auto ack = decodeTypedControlAck(record);
        if (!ack) return;
        payload.insert(QStringLiteral("last_control_ack"),
                       QJsonObject{{QStringLiteral("mono_us"), u64Text(ack->monoUs)},
                                   {QStringLiteral("command_id"), u64Text(ack->commandId)},
                                   {QStringLiteral("status"), int(ack->status)},
                                   {QStringLiteral("reason"), int(ack->reason)},
                                   {QStringLiteral("target_bus"), int(ack->targetBus)},
                                   {QStringLiteral("target_dlc_flags"), int(ack->targetDlcFlags)},
                                   {QStringLiteral("target_can_id"), u64Text(ack->targetCanId)},
                                   {QStringLiteral("target_extended"), ack->targetExtended},
                                   {QStringLiteral("target_rtr"), ack->targetRtr},
                                   {QStringLiteral("counter"), u64Text(ack->counter)},
                                   {QStringLiteral("rejected_total"), u64Text(ack->rejectedTotal)}});
        payload.insert(QStringLiteral("control_ack_total"), u64Text(ack->counter));
        payload.insert(QStringLiteral("control_ack_rejected_total"), u64Text(ack->rejectedTotal));
    } else if (type == TypedRecordType::CanTxRaw) {
        const auto can = decodeTypedCanRaw(record);
        if (!can || !can->txAudit) return;
        payload.insert(QStringLiteral("last_can_tx_audit"),
                       QJsonObject{{QStringLiteral("mono_us"), u64Text(can->monoUs)},
                                   {QStringLiteral("bus"), int(can->bus)},
                                   {QStringLiteral("can_id"), u64Text(can->canId)},
                                   {QStringLiteral("dlc"), int(can->dlc)},
                                   {QStringLiteral("total"), u64Text(can->total)},
                                   {QStringLiteral("failed_total"), u64Text(can->droppedOrFailed)}});
        payload.insert(QStringLiteral("can_tx_audit_total"), u64Text(can->total));
        if (can->droppedOrFailed > 0) {
            payload.insert(QStringLiteral("can_tx_failed_total"), u64Text(can->droppedOrFailed));
            severity = maxSeverity(severity, CanMonitorCore::CoreViewSeverity::Error);
        }
    } else if (type == TypedRecordType::CanRxRaw) {
        const auto can = decodeTypedCanRaw(record);
        if (!can || can->txAudit) return;
        payload.insert(QStringLiteral("last_control_feedback_rx"),
                       QJsonObject{{QStringLiteral("mono_us"), u64Text(can->monoUs)},
                                   {QStringLiteral("bus"), int(can->bus)},
                                   {QStringLiteral("can_id"), u64Text(can->canId)},
                                   {QStringLiteral("dlc"), int(can->dlc)}});
    } else {
        return;
    }

    m_coreEvidenceTransportPayload = payload;
    m_coreEvidenceCheapCounts = counts;
    m_coreEvidenceSeverity = severity;
    result.viewChanges.push_back(m_viewStore.updateView(CanMonitorCore::CoreViewName::TransportSummary,
                                                        m_coreEvidenceTransportPayload,
                                                        m_coreEvidenceSeverity,
                                                        m_coreEvidenceCheapCounts));
}

void CaptureCoreRuntime::updateLiveLatestView(const QVector<CanRxLite>& frames, Result& result) {
    if (frames.isEmpty()) return;

    quint64 droppedThisUpdate = 0;
    for (const CanRxLite& frame : frames) {
        const quint64 key = liveLatestKeyForFrame(frame);
        if (!m_liveLatestByKey.contains(key) && m_liveLatestByKey.size() >= kCoreLiveLatestMaxKeys) {
            auto oldest = m_liveLatestByKey.begin();
            for (auto it = m_liveLatestByKey.begin(); it != m_liveLatestByKey.end(); ++it) {
                if (it.value().monoUs < oldest.value().monoUs) {
                    oldest = it;
                }
            }
            m_liveLatestByKey.erase(oldest);
            ++m_liveLatestDropped;
            ++droppedThisUpdate;
        }
        m_liveLatestByKey.insert(key, frame);
    }

    QVector<CanRxLite> latestFrames;
    latestFrames.reserve(m_liveLatestByKey.size());
    for (auto it = m_liveLatestByKey.cbegin(); it != m_liveLatestByKey.cend(); ++it) {
        latestFrames.push_back(it.value());
    }
    std::sort(latestFrames.begin(), latestFrames.end(), [](const CanRxLite& a, const CanRxLite& b) {
        if (a.monoUs != b.monoUs) return a.monoUs < b.monoUs;
        if (a.bus != b.bus) return a.bus < b.bus;
        if (a.extended != b.extended) return a.extended < b.extended;
        if (a.rtr != b.rtr) return a.rtr < b.rtr;
        return a.canId < b.canId;
    });

    QJsonArray rows;
    for (const CanRxLite& frame : latestFrames) {
        rows.append(frameToViewRow(frame));
    }

    QJsonObject counts;
    counts.insert(QStringLiteral("key_count"), latestFrames.size());
    counts.insert(QStringLiteral("key_cap"), kCoreLiveLatestMaxKeys);
    counts.insert(QStringLiteral("dropped_display_count"), QString::number(m_liveLatestDropped));
    counts.insert(QStringLiteral("source"), QStringLiteral("live_projection"));

    result.viewChanges.push_back(m_viewStore.updateArrayView(CanMonitorCore::CoreViewName::LiveLatest,
                                                             QStringLiteral("frames"),
                                                             rows,
                                                             CanMonitorCore::CoreViewSeverity::Ok,
                                                             counts,
                                                             captureRangeForFrames(latestFrames),
                                                             -1,
                                                             droppedThisUpdate));
}

void CaptureCoreRuntime::updateStatusViews(const Result& ingestResult, Result& out) {
    if (ingestResult.typedStatusDue || ingestResult.projectionStatusDue || ingestResult.latestStatusDue) {
        const auto projection = m_liveProjection.status();
        const auto latest = m_liveLatest.status();
        QJsonObject payload;
        payload.insert(QStringLiteral("typed_frames"), QString::number(ingestResult.typedStatus.frames));
        payload.insert(QStringLiteral("typed_bytes_dropped"), QString::number(ingestResult.typedStatus.bytesDropped));
        payload.insert(QStringLiteral("typed_crc_failures"), QString::number(ingestResult.typedStatus.crcFailures));
        payload.insert(QStringLiteral("typed_length_failures"), QString::number(ingestResult.typedStatus.lengthFailures));
        payload.insert(QStringLiteral("typed_version_warnings"), QString::number(ingestResult.typedStatus.versionWarnings));
        payload.insert(QStringLiteral("typed_seq_gaps"), QString::number(ingestResult.typedStatus.seqGaps));
        payload.insert(QStringLiteral("projection_observed_can_rx"), QString::number(projection.observedCanRxFrames));
        payload.insert(QStringLiteral("projection_projected_can_rx"), QString::number(projection.projectedCanRxFrames));
        payload.insert(QStringLiteral("projection_sampled_can_rx"), QString::number(projection.sampledCanRxFrames));
        payload.insert(QStringLiteral("projection_dropped_can_rx"), QString::number(projection.workerDroppedCanRxFrames));
        payload.insert(QStringLiteral("projection_observed_bus0_can_rx"), QString::number(projection.observedBus0CanRxFrames));
        payload.insert(QStringLiteral("projection_observed_bus1_can_rx"), QString::number(projection.observedBus1CanRxFrames));
        payload.insert(QStringLiteral("projection_observed_control"), QString::number(projection.observedControlEvidenceRecords));
        payload.insert(QStringLiteral("projection_projected_control"), QString::number(projection.projectedControlEvidenceRecords));
        payload.insert(QStringLiteral("projection_sampled_control"), QString::number(projection.sampledControlEvidenceRecords));
        payload.insert(QStringLiteral("projection_last_input_records"), projection.lastInputRecords);
        payload.insert(QStringLiteral("projection_last_output_frames"), projection.lastOutputFrames);
        payload.insert(QStringLiteral("latest_observed_can_rx"), QString::number(latest.observedCanRxFrames));
        payload.insert(QStringLiteral("latest_emitted_frames"), QString::number(latest.emittedLatestFrames));
        payload.insert(QStringLiteral("latest_coalesced_updates"), QString::number(latest.coalescedLatestUpdates));
        payload.insert(QStringLiteral("latest_observed_bus0_can_rx"), QString::number(latest.observedBus0CanRxFrames));
        payload.insert(QStringLiteral("latest_observed_bus1_can_rx"), QString::number(latest.observedBus1CanRxFrames));
        payload.insert(QStringLiteral("latest_flush_count"), QString::number(latest.flushCount));
        payload.insert(QStringLiteral("latest_pending_keys"), latest.pendingKeys);
        payload.insert(QStringLiteral("latest_max_pending_keys"), latest.maxPendingKeys);
        payload.insert(QStringLiteral("latest_last_input_records"), latest.lastInputRecords);
        payload.insert(QStringLiteral("latest_last_output_frames"), latest.lastOutputFrames);
        payload.insert(QStringLiteral("latest_last_flush_ms"), latest.lastFlushMs);
        payload.insert(QStringLiteral("latest_display_loss"), QString::number(latest.displayLoss));
        QJsonObject counts;
        counts.insert(QStringLiteral("typed_frames"), QString::number(ingestResult.typedStatus.frames));
        counts.insert(QStringLiteral("seq_gaps"), QString::number(ingestResult.typedStatus.seqGaps));

        for (auto it = m_coreEvidenceTransportPayload.constBegin(); it != m_coreEvidenceTransportPayload.constEnd(); ++it) {
            payload.insert(it.key(), it.value());
        }
        for (auto it = m_coreEvidenceCheapCounts.constBegin(); it != m_coreEvidenceCheapCounts.constEnd(); ++it) {
            counts.insert(it.key(), it.value());
        }

        const bool fatal = ingestResult.typedStatus.crcFailures > 0 || ingestResult.typedStatus.lengthFailures > 0;
        const auto severity = maxSeverity(fatal ? CanMonitorCore::CoreViewSeverity::Error : CanMonitorCore::CoreViewSeverity::Ok,
                                          m_coreEvidenceSeverity);
        out.viewChanges.push_back(m_viewStore.updateView(CanMonitorCore::CoreViewName::TransportSummary,
                                                         payload,
                                                         severity,
                                                         counts));
    }

    if (m_options.captureRecords || ingestResult.captureHandoffOverrun) {
        QJsonObject payload;
        payload.insert(QStringLiteral("capture_enabled"), m_options.captureRecords);
        payload.insert(QStringLiteral("capture_drain_needed"), ingestResult.captureDrainNeeded);
        payload.insert(QStringLiteral("capture_handoff_overrun"), ingestResult.captureHandoffOverrun);
        payload.insert(QStringLiteral("capture_handoff_overrun_records"), QString::number(ingestResult.captureHandoffOverrunRecords));
        payload.insert(QStringLiteral("capture_handoff_overrun_bytes"), QString::number(ingestResult.captureHandoffOverrunBytes));
        if (!ingestResult.captureHandoffError.isEmpty()) {
            payload.insert(QStringLiteral("capture_handoff_error"), ingestResult.captureHandoffError);
        }

        QJsonObject counts;
        counts.insert(QStringLiteral("handoff_overrun_records"), QString::number(ingestResult.captureHandoffOverrunRecords));
        counts.insert(QStringLiteral("handoff_overrun_bytes"), QString::number(ingestResult.captureHandoffOverrunBytes));

        out.viewChanges.push_back(m_viewStore.updateView(CanMonitorCore::CoreViewName::CaptureProgress,
                                                         payload,
                                                         ingestResult.captureHandoffOverrun
                                                             ? CanMonitorCore::CoreViewSeverity::Fatal
                                                             : CanMonitorCore::CoreViewSeverity::Ok,
                                                         counts));
    }
}

void CaptureCoreRuntime::appendCanRxFrames(const TypedRecord& record, QVector<CanRxLite>& out) const {
    if (record.isType(TypedRecordType::CanRxRaw)) {
        const auto can = decodeTypedCanRaw(record);
        if (can && !can->txAudit) out.push_back(canRxLiteFromCanRawRecord(record, *can));
        return;
    }
    if (!record.isType(TypedRecordType::CanRxSegment)) return;
    const auto header = decodeTypedCanRxSegmentHeader(record);
    if (!header) return;
    out.reserve(out.size() + header->frameCount);
    for (qsizetype index = 0; index < header->frameCount; ++index) {
        const auto entry = decodeTypedCanRxSegmentEntry(record, index);
        if (entry) out.push_back(canRxLiteFromSegmentEntryRecord(record, *entry));
    }
}

TypedIngressRuntime::HandshakeWatchdogState CaptureCoreRuntime::evaluateHandshake(qint64 elapsedMs,
                                                                                  qint64 timeoutMs) const {
    return m_pipeline.evaluateHandshake(elapsedMs, timeoutMs);
}

QJsonObject CaptureCoreRuntime::makeCaptureDiagnostics() const {
    return m_pipeline.makeCaptureDiagnostics();
}

CanMonitorCore::ViewQueryResult CaptureCoreRuntime::queryView(const CanMonitorCore::ViewQuery& query) const {
    return m_viewStore.queryView(query);
}

QVector<CanMonitorCore::ViewChanged> CaptureCoreRuntime::viewChanges() const {
    return m_viewStore.changes();
}

quint64 CaptureCoreRuntime::liveLatestKeyForFrame(const CanRxLite& frame) {
    quint64 key = (quint64(frame.bus) << 56);
    if (frame.extended) key |= (quint64(1) << 55);
    if (frame.rtr) key |= (quint64(1) << 54);
    key |= quint64(frame.canId & 0x1FFFFFFFU);
    return key;
}

} // namespace CanMonitorTransport
