#include "transport/CaptureCoreRuntime.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cstring>

namespace {
constexpr int kCoreLocalBatchSize = 128;
constexpr int kCoreLiveLatestMaxKeys = 256;

FrameRecord frameFromCanRawRecord(const TypedRecord& record, const TypedCanRawRecord& can) {
    FrameRecord frame;
    frame.tExtUs = can.monoUs;
    frame.canId = can.canId;
    frame.ext = can.extended;
    frame.rtr = can.rtr;
    frame.dlc = can.dlc;
    frame.bus = can.bus;
    frame.seq = quint8(record.header.seq & 0xFF);
    std::memcpy(frame.data, can.data, sizeof(frame.data));
    return frame;
}

FrameRecord frameFromSegmentEntryRecord(const TypedRecord& record, const TypedCanRxSegmentEntry& entry) {
    FrameRecord frame;
    frame.tExtUs = entry.monoUs;
    frame.canId = entry.canId;
    frame.ext = entry.extended;
    frame.rtr = entry.rtr;
    frame.dlc = entry.dlc;
    frame.bus = entry.bus;
    frame.seq = quint8(record.header.seq & 0xFF);
    frame.hasCaptureSeq = true;
    frame.captureSeq = entry.captureSeq;
    std::memcpy(frame.data, entry.data, sizeof(frame.data));
    return frame;
}

QString frameDataHex(const FrameRecord& frame) {
    QByteArray bytes;
    bytes.reserve(8);
    for (quint8 byte : frame.data) {
        bytes.append(char(byte));
    }
    return QString::fromLatin1(bytes.toHex());
}

QJsonObject frameToViewRow(const FrameRecord& frame) {
    QJsonObject row;
    row.insert(QStringLiteral("mono_us"), QString::number(frame.tExtUs));
    row.insert(QStringLiteral("bus"), int(frame.bus));
    row.insert(QStringLiteral("can_id"), int(frame.canId));
    row.insert(QStringLiteral("ext"), frame.ext);
    row.insert(QStringLiteral("rtr"), frame.rtr);
    row.insert(QStringLiteral("dlc"), int(frame.dlc));
    row.insert(QStringLiteral("data_hex"), frameDataHex(frame));
    row.insert(QStringLiteral("seq"), int(frame.seq));
    if (frame.hasCaptureSeq) {
        row.insert(QStringLiteral("capture_seq"), QString::number(frame.captureSeq));
    }
    return row;
}

CanMonitorCore::CaptureSeqRange captureRangeForFrames(const QVector<FrameRecord>& frames) {
    CanMonitorCore::CaptureSeqRange range;
    for (const FrameRecord& frame : frames) {
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
    m_liveTruth.reset();
    m_viewStore.clear();
    m_liveLatestByKey.clear();
    m_liveLatestDropped = 0;
}

CaptureCoreRuntime::Result CaptureCoreRuntime::ingestBlocks(const QVector<DrainByteQueue::Block>& blocks,
                                                            qint64 handshakeElapsedMs,
                                                            quint64 parseBacklogBytes) {
    Result out;
    TypedRecordList localBatch;
    localBatch.reserve(kCoreLocalBatchSize);
    auto flushLocalBatch = [this, &localBatch, &out]() {
        if (localBatch.isEmpty()) return;
        ingestBatch(std::move(localBatch), out);
        localBatch.clear();
        localBatch.reserve(kCoreLocalBatchSize);
    };

    auto result = m_pipeline.ingestBlocksEach(blocks,
                                              handshakeElapsedMs,
                                              parseBacklogBytes,
                                              m_options.captureRecords,
                                              [&localBatch, &flushLocalBatch](TypedRecord&& record) {
                                                  localBatch.push_back(std::move(record));
                                                  if (localBatch.size() >= kCoreLocalBatchSize) flushLocalBatch();
                                              });
    flushLocalBatch();
    out.capabilityFirstSeen = result.capabilityFirstSeen;
    out.capabilityElapsedMs = result.capabilityElapsedMs;
    out.capabilityBytes = result.capabilityBytes;
    out.errors = result.errors;
    out.typedStatusDue = result.statusDue;
    out.typedStatus = result.status;
    updateStatusViews(out, out);
    return out;
}

void CaptureCoreRuntime::ingestBatch(TypedRecordList&& batch, Result& result) {
    if (batch.isEmpty()) return;

    if (m_options.emitCanRxFrames) {
        for (const TypedRecord& record : batch) {
            appendCanRxFrames(record, result.canRxFrames);
        }
    }

    const auto projection = m_liveProjection.ingest(batch);
    if (!projection.criticalRecords.isEmpty()) {
        result.criticalRecords += projection.criticalRecords;
    }
    if (m_options.emitProjectionFrames && !projection.projectedFrames.isEmpty()) {
        result.projectedFrames += projection.projectedFrames;
    }
    updateLiveLatestView(projection.projectedFrames, result);
    if (projection.statusDue) {
        result.projectionStatusDue = true;
        result.projectionStatus = projection.status;
    }

    if (m_options.emitTruthFrames) {
        const auto truth = m_liveTruth.ingest(batch);
        if (!truth.frames.isEmpty()) {
            result.truthFrames += truth.frames;
        }
        if (truth.statusDue) {
            result.truthStatusDue = true;
            result.truthStatus = truth.status;
        }
    }

    if (m_options.captureRecords && m_captureQueue) {
        auto push = m_captureQueue->push(std::move(batch));
        result.captureDrainNeeded = result.captureDrainNeeded || push.shouldScheduleDrain;
        if (!push.accepted && push.records > 0) {
            result.captureHandoffOverrun = true;
            result.captureHandoffError = push.error;
            result.captureHandoffOverrunRecords += push.records;
            result.captureHandoffOverrunBytes += push.bytes;
        }
    }
}

void CaptureCoreRuntime::updateLiveLatestView(const FrameRecordList& frames, Result& result) {
    if (frames.isEmpty()) return;

    quint64 droppedThisUpdate = 0;
    for (const FrameRecord& frame : frames) {
        const quint64 key = liveLatestKeyForFrame(frame);
        if (!m_liveLatestByKey.contains(key) && m_liveLatestByKey.size() >= kCoreLiveLatestMaxKeys) {
            auto oldest = m_liveLatestByKey.begin();
            for (auto it = m_liveLatestByKey.begin(); it != m_liveLatestByKey.end(); ++it) {
                if (it.value().tExtUs < oldest.value().tExtUs) {
                    oldest = it;
                }
            }
            m_liveLatestByKey.erase(oldest);
            ++m_liveLatestDropped;
            ++droppedThisUpdate;
        }
        m_liveLatestByKey.insert(key, frame);
    }

    QVector<FrameRecord> latestFrames;
    latestFrames.reserve(m_liveLatestByKey.size());
    for (auto it = m_liveLatestByKey.cbegin(); it != m_liveLatestByKey.cend(); ++it) {
        latestFrames.push_back(it.value());
    }
    std::sort(latestFrames.begin(), latestFrames.end(), [](const FrameRecord& a, const FrameRecord& b) {
        if (a.tExtUs != b.tExtUs) return a.tExtUs < b.tExtUs;
        if (a.bus != b.bus) return a.bus < b.bus;
        if (a.ext != b.ext) return a.ext < b.ext;
        if (a.rtr != b.rtr) return a.rtr < b.rtr;
        return a.canId < b.canId;
    });

    QJsonArray rows;
    for (const FrameRecord& frame : latestFrames) {
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
    if (ingestResult.typedStatusDue || ingestResult.projectionStatusDue || ingestResult.truthStatusDue) {
        const auto projection = m_liveProjection.status();
        const auto truth = m_liveTruth.status();
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
        payload.insert(QStringLiteral("truth_observed_can_rx"), QString::number(truth.observedCanRxFrames));
        payload.insert(QStringLiteral("truth_emitted_frames"), QString::number(truth.emittedTruthFrames));
        payload.insert(QStringLiteral("truth_coalesced_updates"), QString::number(truth.coalescedTruthUpdates));
        payload.insert(QStringLiteral("truth_observed_bus0_can_rx"), QString::number(truth.observedBus0CanRxFrames));
        payload.insert(QStringLiteral("truth_observed_bus1_can_rx"), QString::number(truth.observedBus1CanRxFrames));
        payload.insert(QStringLiteral("truth_flush_count"), QString::number(truth.flushCount));
        payload.insert(QStringLiteral("truth_pending_keys"), truth.pendingKeys);
        payload.insert(QStringLiteral("truth_max_pending_keys"), truth.maxPendingKeys);
        payload.insert(QStringLiteral("truth_last_input_records"), truth.lastInputRecords);
        payload.insert(QStringLiteral("truth_last_output_frames"), truth.lastOutputFrames);
        payload.insert(QStringLiteral("truth_last_flush_ms"), truth.lastFlushMs);
        payload.insert(QStringLiteral("truth_loss"), QString::number(truth.truthLoss));

        QJsonObject counts;
        counts.insert(QStringLiteral("typed_frames"), QString::number(ingestResult.typedStatus.frames));
        counts.insert(QStringLiteral("seq_gaps"), QString::number(ingestResult.typedStatus.seqGaps));

        const bool fatal = ingestResult.typedStatus.crcFailures > 0 || ingestResult.typedStatus.lengthFailures > 0;
        out.viewChanges.push_back(m_viewStore.updateView(CanMonitorCore::CoreViewName::TransportSummary,
                                                         payload,
                                                         fatal ? CanMonitorCore::CoreViewSeverity::Error : CanMonitorCore::CoreViewSeverity::Ok,
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

void CaptureCoreRuntime::appendCanRxFrames(const TypedRecord& record, FrameRecordList& out) const {
    if (record.isType(TypedRecordType::CanRxRaw)) {
        const auto can = decodeTypedCanRaw(record);
        if (can && !can->txAudit) out.push_back(frameFromCanRawRecord(record, *can));
        return;
    }
    if (!record.isType(TypedRecordType::CanRxSegment)) return;
    const auto header = decodeTypedCanRxSegmentHeader(record);
    if (!header) return;
    out.reserve(out.size() + header->frameCount);
    for (qsizetype index = 0; index < header->frameCount; ++index) {
        const auto entry = decodeTypedCanRxSegmentEntry(record, index);
        if (entry) out.push_back(frameFromSegmentEntryRecord(record, *entry));
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

FrameRecordList CaptureCoreRuntime::flushTruth(bool force) {
    return m_liveTruth.flush(force);
}

quint64 CaptureCoreRuntime::liveLatestKeyForFrame(const FrameRecord& frame) {
    quint64 key = (quint64(frame.bus) << 56);
    if (frame.ext) key |= (quint64(1) << 55);
    if (frame.rtr) key |= (quint64(1) << 54);
    key |= quint64(frame.canId & 0x1FFFFFFFU);
    return key;
}

} // namespace CanMonitorTransport
