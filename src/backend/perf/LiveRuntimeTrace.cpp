#include "perf/LiveRuntimeTrace.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace CanMonitorPerf {
namespace {

constexpr int kTraceSnapshotIntervalMs = 500;
constexpr int kHeartbeatIntervalMs = 100;

qint64 nowWallMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

void atomicMax(std::atomic<qint64>& target, qint64 value) {
    qint64 current = target.load(std::memory_order_acquire);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }
}

QJsonObject processMemoryJson() {
    QJsonObject obj;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters;
    std::memset(&counters, 0, sizeof(counters));
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        obj.insert(QStringLiteral("private_bytes"), QString::number(qulonglong(counters.PrivateUsage)));
        obj.insert(QStringLiteral("working_set"), QString::number(qulonglong(counters.WorkingSetSize)));
        obj.insert(QStringLiteral("peak_working_set"), QString::number(qulonglong(counters.PeakWorkingSetSize)));
        obj.insert(QStringLiteral("pagefile_usage"), QString::number(qulonglong(counters.PagefileUsage)));
        obj.insert(QStringLiteral("peak_pagefile_usage"), QString::number(qulonglong(counters.PeakPagefileUsage)));
        return obj;
    }
#endif
    obj.insert(QStringLiteral("private_bytes"), QStringLiteral("0"));
    obj.insert(QStringLiteral("working_set"), QStringLiteral("0"));
    obj.insert(QStringLiteral("peak_working_set"), QStringLiteral("0"));
    obj.insert(QStringLiteral("pagefile_usage"), QStringLiteral("0"));
    obj.insert(QStringLiteral("peak_pagefile_usage"), QStringLiteral("0"));
    return obj;
}

quint64 jsonCounter(const QJsonObject& obj, const QString& key) {
    const QJsonValue value = obj.value(key);
    if (value.isString()) return value.toString().toULongLong();
    if (value.isDouble()) return quint64(std::max<double>(0.0, value.toDouble()));
    return 0;
}

qint64 percentile95(QVector<qint64> values) {
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    const int valueCount = int(values.size());
    const int index = std::clamp((valueCount * 95 + 99) / 100 - 1, 0, valueCount - 1);
    return values.at(index);
}

} // namespace

QJsonObject RuntimeOwnerSnapshot::toJson() const {
    QJsonObject obj;
    obj.insert(QStringLiteral("live_model_rows"), QString::number(liveModelRows));
    obj.insert(QStringLiteral("raw_ledger_rows"), QString::number(rawLedgerRows));
    obj.insert(QStringLiteral("raw_ledger_visible_rows"), QString::number(rawLedgerVisibleRows));
    obj.insert(QStringLiteral("raw_ledger_cache_rows"), QString::number(rawLedgerCacheRows));
    obj.insert(QStringLiteral("decoded_tail_rows"), QString::number(rawLedgerRows));
    obj.insert(QStringLiteral("decoded_tail_visible_rows"), QString::number(rawLedgerVisibleRows));
    obj.insert(QStringLiteral("decoded_tail_cache_rows"), QString::number(rawLedgerCacheRows));
    obj.insert(QStringLiteral("graph_series_count"), QString::number(graphSeriesCount));
    obj.insert(QStringLiteral("graph_selected_keys"), QString::number(graphSelectedKeys));
    obj.insert(QStringLiteral("live_graph_series"), QString::number(liveGraphSeries));
    obj.insert(QStringLiteral("live_graph_points_estimate"), QString::number(liveGraphPointsEstimate));
    obj.insert(QStringLiteral("pending_live_rows"), QString::number(pendingLiveRows));
    obj.insert(QStringLiteral("pending_live_view_rows"), QString::number(pendingLiveViewRows));
    obj.insert(QStringLiteral("analysis_timing_rows"), QString::number(analysisTimingRows));
    obj.insert(QStringLiteral("analysis_value_rows"), QString::number(analysisValueRows));
    obj.insert(QStringLiteral("analysis_alarm_rows"), QString::number(analysisAlarmRows));
    obj.insert(QStringLiteral("diagnostics_rows"), QString::number(diagnosticsRows));
    obj.insert(QStringLiteral("capture_writer_queue_bytes"), QString::number(captureWriterQueueBytes));
    obj.insert(QStringLiteral("capture_writer_max_queue_bytes"), QString::number(captureWriterMaxQueueBytes));
    obj.insert(QStringLiteral("capture_writer_overrun_bytes"), QString::number(captureWriterOverrunBytes));
    obj.insert(QStringLiteral("raw_ledger_writer_queue_bytes"), QString::number(rawLedgerWriterQueueBytes));
    obj.insert(QStringLiteral("raw_ledger_writer_max_queue_bytes"), QString::number(rawLedgerWriterMaxQueueBytes));
    obj.insert(QStringLiteral("raw_ledger_writer_overrun_bytes"), QString::number(rawLedgerWriterOverrunBytes));
    obj.insert(QStringLiteral("analysis_queue_frames"), QString::number(analysisQueueFrames));
    obj.insert(QStringLiteral("analysis_queue_max_frames"), QString::number(analysisQueueMaxFrames));
    obj.insert(QStringLiteral("analysis_queue_overrun_frames"), QString::number(analysisQueueOverrunFrames));
    return obj;
}

LiveRuntimeTraceRegistry& LiveRuntimeTraceRegistry::instance() {
    static LiveRuntimeTraceRegistry registry;
    return registry;
}

const char* LiveRuntimeTraceRegistry::signalName(LiveTraceSignal signal) {
    switch (signal) {
    case LiveTraceSignal::FramesReceived: return "framesReceived";
    case LiveTraceSignal::typedLiveLatestStatusChanged: return "typedLiveLatestStatusChanged";
    case LiveTraceSignal::TypedProjectionStatusChanged: return "typedProjectionStatusChanged";
    case LiveTraceSignal::TypedTransportStatusChanged: return "typedTransportStatusChanged";
    case LiveTraceSignal::TransportDiagnosticsChanged: return "transportDiagnosticsChanged";
    case LiveTraceSignal::AnalysisRuntimeSnapshotChanged: return "analysisRuntimeSnapshotChanged";
    case LiveTraceSignal::RawLedgerBatchCommitted: return "rawLedgerBatchCommitted";
    case LiveTraceSignal::Count: break;
    }
    return "unknown";
}

void LiveRuntimeTraceRegistry::setEnabled(bool enabled) {
    m_enabled.store(enabled, std::memory_order_release);
}

bool LiveRuntimeTraceRegistry::enabled() const {
    return m_enabled.load(std::memory_order_acquire);
}

void LiveRuntimeTraceRegistry::reset() {
    for (SignalCounters& signal : m_signals) {
        signal.emitSeq.store(0, std::memory_order_release);
        signal.slotSeq.store(0, std::memory_order_release);
        signal.lastEmitWallMs.store(0, std::memory_order_release);
        signal.lastSlotWallMs.store(0, std::memory_order_release);
        signal.lastSlotDelayMs.store(-1, std::memory_order_release);
        signal.maxSlotDelayMs.store(0, std::memory_order_release);
        signal.payloadCountEst.store(0, std::memory_order_release);
        signal.payloadBytesEst.store(0, std::memory_order_release);
    }
    m_heartbeat.sent.store(0, std::memory_order_release);
    m_heartbeat.ack.store(0, std::memory_order_release);
    m_heartbeat.inflight.store(false, std::memory_order_release);
    m_heartbeat.pendingMs.store(0, std::memory_order_release);
    m_heartbeat.lastDelayMs.store(0, std::memory_order_release);
    m_heartbeat.maxDelayMs.store(0, std::memory_order_release);
    m_heartbeat.stall100Ms.store(0, std::memory_order_release);
    m_heartbeat.stall500Ms.store(0, std::memory_order_release);
    m_heartbeat.stall1000Ms.store(0, std::memory_order_release);
    m_heartbeat.recentCount.store(0, std::memory_order_release);
    m_heartbeat.recentCursor.store(0, std::memory_order_release);
    for (auto& value : m_heartbeat.recent) value.store(0, std::memory_order_release);
}

void LiveRuntimeTraceRegistry::noteEmit(LiveTraceSignal signal, quint64 payloadCount, quint64 payloadBytes) {
    if (!enabled()) return;
    const int index = static_cast<int>(signal);
    if (index < 0 || index >= static_cast<int>(LiveTraceSignal::Count)) return;
    SignalCounters& counters = m_signals.at(index);
    counters.emitSeq.fetch_add(1, std::memory_order_acq_rel);
    counters.lastEmitWallMs.store(nowWallMs(), std::memory_order_release);
    counters.payloadCountEst.store(payloadCount, std::memory_order_release);
    counters.payloadBytesEst.store(payloadBytes, std::memory_order_release);
}

void LiveRuntimeTraceRegistry::noteSlot(LiveTraceSignal signal, quint64 payloadCount, quint64 payloadBytes) {
    if (!enabled()) return;
    const int index = static_cast<int>(signal);
    if (index < 0 || index >= static_cast<int>(LiveTraceSignal::Count)) return;
    SignalCounters& counters = m_signals.at(index);
    counters.slotSeq.fetch_add(1, std::memory_order_acq_rel);
    const qint64 nowMs = nowWallMs();
    const qint64 emitMs = counters.lastEmitWallMs.load(std::memory_order_acquire);
    const qint64 delayMs = emitMs > 0 ? std::max<qint64>(0, nowMs - emitMs) : -1;
    counters.lastSlotWallMs.store(nowMs, std::memory_order_release);
    counters.lastSlotDelayMs.store(delayMs, std::memory_order_release);
    if (delayMs >= 0) atomicMax(counters.maxSlotDelayMs, delayMs);
    if (payloadCount > 0) counters.payloadCountEst.store(payloadCount, std::memory_order_release);
    if (payloadBytes > 0) counters.payloadBytesEst.store(payloadBytes, std::memory_order_release);
}

void LiveRuntimeTraceRegistry::noteHeartbeatSent(qint64 sentWallMs) {
    if (!enabled()) return;
    m_heartbeat.sent.fetch_add(1, std::memory_order_acq_rel);
    m_heartbeat.inflight.store(true, std::memory_order_release);
    m_heartbeat.pendingMs.store(0, std::memory_order_release);
    Q_UNUSED(sentWallMs);
}

void LiveRuntimeTraceRegistry::noteHeartbeatAck(qint64 sentWallMs) {
    if (!enabled()) return;
    const qint64 delayMs = sentWallMs > 0 ? std::max<qint64>(0, nowWallMs() - sentWallMs) : 0;
    m_heartbeat.ack.fetch_add(1, std::memory_order_acq_rel);
    m_heartbeat.inflight.store(false, std::memory_order_release);
    m_heartbeat.pendingMs.store(0, std::memory_order_release);
    m_heartbeat.lastDelayMs.store(delayMs, std::memory_order_release);
    atomicMax(m_heartbeat.maxDelayMs, delayMs);
    if (delayMs >= 100) m_heartbeat.stall100Ms.fetch_add(1, std::memory_order_acq_rel);
    if (delayMs >= 500) m_heartbeat.stall500Ms.fetch_add(1, std::memory_order_acq_rel);
    if (delayMs >= 1000) m_heartbeat.stall1000Ms.fetch_add(1, std::memory_order_acq_rel);
    const quint64 cursor = m_heartbeat.recentCursor.fetch_add(1, std::memory_order_acq_rel);
    m_heartbeat.recent.at(cursor % m_heartbeat.recent.size()).store(delayMs, std::memory_order_release);
    quint64 count = m_heartbeat.recentCount.load(std::memory_order_acquire);
    while (count < m_heartbeat.recent.size() &&
           !m_heartbeat.recentCount.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }
}

void LiveRuntimeTraceRegistry::noteHeartbeatPending(qint64 pendingMs) {
    if (!enabled()) return;
    if (pendingMs < 0) return;
    m_heartbeat.pendingMs.store(pendingMs, std::memory_order_release);
    atomicMax(m_heartbeat.maxDelayMs, pendingMs);
}

QVector<LiveRuntimeTraceRegistry::SignalSnapshot> LiveRuntimeTraceRegistry::signalSnapshots() const {
    QVector<SignalSnapshot> out;
    out.reserve(static_cast<int>(LiveTraceSignal::Count));
    for (int i = 0; i < static_cast<int>(LiveTraceSignal::Count); ++i) {
        const SignalCounters& counters = m_signals.at(i);
        SignalSnapshot s;
        s.name = QString::fromLatin1(signalName(static_cast<LiveTraceSignal>(i)));
        s.emitSeq = counters.emitSeq.load(std::memory_order_acquire);
        s.slotSeq = counters.slotSeq.load(std::memory_order_acquire);
        s.inflight = s.emitSeq > s.slotSeq ? s.emitSeq - s.slotSeq : 0;
        s.lastEmitWallMs = counters.lastEmitWallMs.load(std::memory_order_acquire);
        s.lastSlotWallMs = counters.lastSlotWallMs.load(std::memory_order_acquire);
        s.lastSlotDelayMs = counters.lastSlotDelayMs.load(std::memory_order_acquire);
        s.maxSlotDelayMs = counters.maxSlotDelayMs.load(std::memory_order_acquire);
        s.payloadCountEst = counters.payloadCountEst.load(std::memory_order_acquire);
        s.payloadBytesEst = counters.payloadBytesEst.load(std::memory_order_acquire);
        out.push_back(s);
    }
    return out;
}

LiveRuntimeTraceRegistry::HeartbeatSnapshot LiveRuntimeTraceRegistry::heartbeatSnapshot() const {
    HeartbeatSnapshot s;
    s.sent = m_heartbeat.sent.load(std::memory_order_acquire);
    s.ack = m_heartbeat.ack.load(std::memory_order_acquire);
    s.inflight = m_heartbeat.inflight.load(std::memory_order_acquire);
    s.pendingMs = m_heartbeat.pendingMs.load(std::memory_order_acquire);
    s.lastDelayMs = m_heartbeat.lastDelayMs.load(std::memory_order_acquire);
    s.maxDelayMs = m_heartbeat.maxDelayMs.load(std::memory_order_acquire);
    s.stall100Ms = m_heartbeat.stall100Ms.load(std::memory_order_acquire);
    s.stall500Ms = m_heartbeat.stall500Ms.load(std::memory_order_acquire);
    s.stall1000Ms = m_heartbeat.stall1000Ms.load(std::memory_order_acquire);
    QVector<qint64> recent;
    const quint64 count = std::min<quint64>(m_heartbeat.recentCount.load(std::memory_order_acquire), m_heartbeat.recent.size());
    recent.reserve(int(count));
    for (quint64 i = 0; i < count; ++i) recent.push_back(m_heartbeat.recent.at(i).load(std::memory_order_acquire));
    s.p95DelayMs = percentile95(recent);
    return s;
}

QJsonObject LiveRuntimeTraceRegistry::toJson(qint64 nowMs) const {
    QJsonObject root;
    root.insert(QStringLiteral("now_wall_ms"), QString::number(quint64(std::max<qint64>(0, nowMs))));
    QJsonArray signalArray;
    for (const SignalSnapshot& s : signalSnapshots()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), s.name);
        obj.insert(QStringLiteral("emit_seq"), QString::number(s.emitSeq));
        obj.insert(QStringLiteral("slot_seq"), QString::number(s.slotSeq));
        obj.insert(QStringLiteral("inflight"), QString::number(s.inflight));
        obj.insert(QStringLiteral("last_emit_wall_ms"), QString::number(quint64(std::max<qint64>(0, s.lastEmitWallMs))));
        obj.insert(QStringLiteral("last_slot_wall_ms"), QString::number(quint64(std::max<qint64>(0, s.lastSlotWallMs))));
        obj.insert(QStringLiteral("last_slot_delay_ms"), s.lastSlotDelayMs);
        obj.insert(QStringLiteral("max_slot_delay_ms"), s.maxSlotDelayMs);
        obj.insert(QStringLiteral("payload_count_est"), QString::number(s.payloadCountEst));
        obj.insert(QStringLiteral("payload_bytes_est"), QString::number(s.payloadBytesEst));
        signalArray.append(obj);
    }
    root.insert(QStringLiteral("signals"), signalArray);

    const HeartbeatSnapshot hb = heartbeatSnapshot();
    QJsonObject heartbeat;
    heartbeat.insert(QStringLiteral("sent"), QString::number(hb.sent));
    heartbeat.insert(QStringLiteral("ack"), QString::number(hb.ack));
    heartbeat.insert(QStringLiteral("inflight"), hb.inflight);
    heartbeat.insert(QStringLiteral("pending_ms"), hb.pendingMs);
    heartbeat.insert(QStringLiteral("last_delay_ms"), hb.lastDelayMs);
    heartbeat.insert(QStringLiteral("max_delay_ms"), hb.maxDelayMs);
    heartbeat.insert(QStringLiteral("p95_delay_ms"), hb.p95DelayMs);
    heartbeat.insert(QStringLiteral("stall_100ms_count"), QString::number(hb.stall100Ms));
    heartbeat.insert(QStringLiteral("stall_500ms_count"), QString::number(hb.stall500Ms));
    heartbeat.insert(QStringLiteral("stall_1000ms_count"), QString::number(hb.stall1000Ms));
    root.insert(QStringLiteral("main_thread_heartbeat"), heartbeat);
    return root;
}

class LiveRuntimeTraceService::Writer : public QObject {
    Q_OBJECT
public:
    explicit Writer(QObject* parent = nullptr)
        : QObject(parent) {
        m_sampleTimer = new QTimer(this);
        m_sampleTimer->setInterval(kTraceSnapshotIntervalMs);
        m_sampleTimer->setTimerType(Qt::CoarseTimer);
        connect(m_sampleTimer, &QTimer::timeout, this, [this]() {
            writeSnapshot();
        });

        m_heartbeatTimer = new QTimer(this);
        m_heartbeatTimer->setInterval(kHeartbeatIntervalMs);
        m_heartbeatTimer->setTimerType(Qt::CoarseTimer);
        connect(m_heartbeatTimer, &QTimer::timeout, this, &Writer::sendHeartbeat);
    }

public slots:
    void startSession(const QString& directory, QObject* mainThreadTarget, bool resetCounters) {
        stopSession(QStringLiteral("restart"));
        m_directory = QDir::fromNativeSeparators(directory);
        if (m_directory.trimmed().isEmpty()) return;
        QDir().mkpath(m_directory);
        m_mainThreadTarget = mainThreadTarget;
        if (resetCounters) LiveRuntimeTraceRegistry::instance().reset();
        LiveRuntimeTraceRegistry::instance().setEnabled(true);

        m_traceFile.setFileName(QDir(m_directory).filePath(QStringLiteral("live_runtime_trace.jsonl")));
        m_metricsFile.setFileName(QDir(m_directory).filePath(QStringLiteral("process_metrics.csv")));
        if (!m_traceFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            LiveRuntimeTraceRegistry::instance().setEnabled(false);
            return;
        }
        if (!m_metricsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            m_traceFile.close();
            LiveRuntimeTraceRegistry::instance().setEnabled(false);
            return;
        }
        QTextStream metrics(&m_metricsFile);
        metrics << "wall_ms,private_bytes,working_set,peak_working_set,pagefile_usage,main_pending_ms,main_max_delay_ms,"
                   "top_signal,top_inflight,top_delay_ms,live_model_rows,raw_ledger_rows,graph_points_estimate,"
                   "pending_live_rows,capture_writer_queue_bytes,raw_ledger_writer_queue_bytes,analysis_queue_frames\n";
        m_metricsFile.flush();
        m_startedWallMs = nowWallMs();
        m_heartbeatInFlight.store(false, std::memory_order_release);
        m_sampleTimer->start();
        m_heartbeatTimer->start();
        writeSnapshot();
    }

    void stopSession(const QString& reason) {
        if (m_sampleTimer && m_sampleTimer->isActive()) writeSnapshot(reason.isEmpty() ? QStringLiteral("stop") : reason);
        if (m_sampleTimer) m_sampleTimer->stop();
        if (m_heartbeatTimer) m_heartbeatTimer->stop();
        if (m_traceFile.isOpen()) {
            m_traceFile.flush();
            m_traceFile.close();
        }
        if (m_metricsFile.isOpen()) {
            m_metricsFile.flush();
            m_metricsFile.close();
        }
        m_directory.clear();
        m_mainThreadTarget.clear();
        m_heartbeatInFlight.store(false, std::memory_order_release);
        LiveRuntimeTraceRegistry::instance().setEnabled(false);
    }

    void flush() {
        writeSnapshot(QStringLiteral("flush"));
        if (m_traceFile.isOpen()) m_traceFile.flush();
        if (m_metricsFile.isOpen()) m_metricsFile.flush();
    }

    void updateOwnerSnapshot(const RuntimeOwnerSnapshot& snapshot) {
        QMutexLocker locker(&m_ownerMutex);
        m_ownerSnapshot = snapshot.toJson();
    }

private slots:
    void sendHeartbeat() {
        if (!m_traceFile.isOpen() || m_heartbeatInFlight.load(std::memory_order_acquire)) {
            const qint64 sent = m_pendingHeartbeatWallMs.load(std::memory_order_acquire);
            if (sent > 0) LiveRuntimeTraceRegistry::instance().noteHeartbeatPending(nowWallMs() - sent);
            return;
        }
        QPointer<QObject> target = m_mainThreadTarget;
        if (!target) return;
        const qint64 sentWallMs = nowWallMs();
        m_pendingHeartbeatWallMs.store(sentWallMs, std::memory_order_release);
        m_heartbeatInFlight.store(true, std::memory_order_release);
        LiveRuntimeTraceRegistry::instance().noteHeartbeatSent(sentWallMs);
        QPointer<Writer> self(this);
        QMetaObject::invokeMethod(target, [sentWallMs, self]() {
            LiveRuntimeTraceRegistry::instance().noteHeartbeatAck(sentWallMs);
            if (self) {
                self->m_heartbeatInFlight.store(false, std::memory_order_release);
                self->m_pendingHeartbeatWallMs.store(0, std::memory_order_release);
            }
        }, Qt::QueuedConnection);
    }

    void writeSnapshot(const QString& reason = QString()) {
        if (!m_traceFile.isOpen() || !m_metricsFile.isOpen()) return;
        const qint64 nowMs = nowWallMs();
        const qint64 sent = m_pendingHeartbeatWallMs.load(std::memory_order_acquire);
        if (m_heartbeatInFlight.load(std::memory_order_acquire) && sent > 0) {
            LiveRuntimeTraceRegistry::instance().noteHeartbeatPending(nowMs - sent);
        }

        QJsonObject owner;
        {
            QMutexLocker locker(&m_ownerMutex);
            owner = m_ownerSnapshot;
        }

        QJsonObject root = LiveRuntimeTraceRegistry::instance().toJson(nowMs);
        root.insert(QStringLiteral("format"), QStringLiteral("vsm-live-runtime-trace-v1"));
        root.insert(QStringLiteral("session_dir"), m_directory);
        root.insert(QStringLiteral("elapsed_ms"), QString::number(quint64(std::max<qint64>(0, nowMs - m_startedWallMs))));
        if (!reason.isEmpty()) root.insert(QStringLiteral("reason"), reason);
        const QJsonObject memory = processMemoryJson();
        root.insert(QStringLiteral("process_memory"), memory);
        root.insert(QStringLiteral("runtime_owner"), owner);
        m_traceFile.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        m_traceFile.write("\n");

        QString topSignal = QStringLiteral("-");
        quint64 topInflight = 0;
        qint64 topDelay = 0;
        for (const auto& signal : LiveRuntimeTraceRegistry::instance().signalSnapshots()) {
            if (signal.inflight > topInflight || (signal.inflight == topInflight && signal.maxSlotDelayMs > topDelay)) {
                topSignal = signal.name;
                topInflight = signal.inflight;
                topDelay = signal.maxSlotDelayMs;
            }
        }
        const auto hb = LiveRuntimeTraceRegistry::instance().heartbeatSnapshot();
        QTextStream metrics(&m_metricsFile);
        metrics << nowMs << ','
                << memory.value(QStringLiteral("private_bytes")).toString() << ','
                << memory.value(QStringLiteral("working_set")).toString() << ','
                << memory.value(QStringLiteral("peak_working_set")).toString() << ','
                << memory.value(QStringLiteral("pagefile_usage")).toString() << ','
                << hb.pendingMs << ','
                << hb.maxDelayMs << ','
                << topSignal << ','
                << topInflight << ','
                << topDelay << ','
                << jsonCounter(owner, QStringLiteral("live_model_rows")) << ','
                << jsonCounter(owner, QStringLiteral("raw_ledger_rows")) << ','
                << jsonCounter(owner, QStringLiteral("live_graph_points_estimate")) << ','
                << jsonCounter(owner, QStringLiteral("pending_live_rows")) << ','
                << jsonCounter(owner, QStringLiteral("capture_writer_queue_bytes")) << ','
                << jsonCounter(owner, QStringLiteral("raw_ledger_writer_queue_bytes")) << ','
                << jsonCounter(owner, QStringLiteral("analysis_queue_frames")) << '\n';
        m_traceFile.flush();
        m_metricsFile.flush();
    }

private:
    QTimer* m_sampleTimer = nullptr;
    QTimer* m_heartbeatTimer = nullptr;
    QFile m_traceFile;
    QFile m_metricsFile;
    QString m_directory;
    QPointer<QObject> m_mainThreadTarget;
    qint64 m_startedWallMs = 0;
    std::atomic_bool m_heartbeatInFlight{false};
    std::atomic<qint64> m_pendingHeartbeatWallMs{0};
    QMutex m_ownerMutex;
    QJsonObject m_ownerSnapshot;
};

LiveRuntimeTraceService::LiveRuntimeTraceService(QObject* parent)
    : QObject(parent)
    , m_writer(new Writer()) {
    m_writer->moveToThread(&m_thread);
    m_thread.setObjectName(QStringLiteral("LiveRuntimeTraceWriter"));
    m_thread.start();
}

LiveRuntimeTraceService::~LiveRuntimeTraceService() {
    stopSession(QStringLiteral("destroy"));
    m_thread.quit();
    m_thread.wait(2000);
    delete m_writer;
}

void LiveRuntimeTraceService::startSession(const QString& directory, QObject* mainThreadTarget, bool resetCounters) {
    QMetaObject::invokeMethod(m_writer,
                              [writer = m_writer, directory, mainThreadTarget, resetCounters]() {
                                  writer->startSession(directory, mainThreadTarget, resetCounters);
                              },
                              Qt::QueuedConnection);
}

void LiveRuntimeTraceService::stopSession(const QString& reason) {
    if (!m_thread.isRunning()) return;
    QMetaObject::invokeMethod(m_writer,
                              [writer = m_writer, reason]() {
                                  writer->stopSession(reason);
                              },
                              Qt::BlockingQueuedConnection);
}

void LiveRuntimeTraceService::flush() {
    if (!m_thread.isRunning()) return;
    QMetaObject::invokeMethod(m_writer,
                              [writer = m_writer]() {
                                  writer->flush();
                              },
                              Qt::BlockingQueuedConnection);
}

void LiveRuntimeTraceService::updateOwnerSnapshot(const RuntimeOwnerSnapshot& snapshot) {
    QMetaObject::invokeMethod(m_writer,
                              [writer = m_writer, snapshot]() {
                                  writer->updateOwnerSnapshot(snapshot);
                              },
                              Qt::QueuedConnection);
}

QString LiveRuntimeTraceService::sessionDirectory() const {
    return QString();
}

} // namespace CanMonitorPerf

#include "LiveRuntimeTrace.moc"
