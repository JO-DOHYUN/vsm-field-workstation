#include "analysis/AnalysisWorkerRuntime.h"

#include <QTimer>
#include <QVariantMap>

#include <algorithm>

namespace {
constexpr int kAnalysisPumpMaxFrames = 4096;
constexpr qint64 kAnalysisPumpBudgetMs = 2;
constexpr qint64 kAnalysisSnapshotIntervalMs = 200;
constexpr qint64 kAnalysisStatusIntervalMs = 250;
}

namespace CanMonitorAnalysis {

AnalysisWorkerRuntime::AnalysisWorkerRuntime(qsizetype capacityFrames, QObject* parent)
    : QObject(parent),
      m_capacityFrames(std::max<qsizetype>(1, capacityFrames)) {}

void AnalysisWorkerRuntime::reset() {
    m_runtime.reset();
    m_queue.clear();
    m_pumpScheduled = false;
    m_statusEmitScheduled = false;
    m_enqueuedFrames = 0;
    m_processedFrames = 0;
    m_overrunFrames = 0;
    m_maxQueuedFrames = 0;
    m_pumpCount = 0;
    m_pumpMaxMs = 0;
    m_snapshotMaxMs = 0;
    m_latestUs = 0;
    m_statusClock.invalidate();
    m_snapshotClock.invalidate();
    emitStatus(true);
}

void AnalysisWorkerRuntime::setConfig(const AnalysisRuntime::Config& config) {
    m_runtime.reset();
    m_runtime.setConfig(config);
    m_queue.clear();
    m_pumpScheduled = false;
    m_statusEmitScheduled = false;
    m_latestUs = 0;
    emitSnapshot(true);
    m_snapshotClock.invalidate();
    emitStatus(true);
}

void AnalysisWorkerRuntime::enqueueFrames(FrameRecordList frames) {
    if (frames.isEmpty()) {
        emit handoffDrained();
        return;
    }

    const qsizetype queued = qsizetype(m_queue.size());
    const qsizetype available = std::max<qsizetype>(0, m_capacityFrames - queued);
    qsizetype accepted = std::min<qsizetype>(available, frames.size());
    if (accepted > 0) {
        for (qsizetype index = 0; index < accepted; ++index) {
            m_queue.push_back(frames.at(index));
        }
        m_enqueuedFrames += quint64(accepted);
    }

    const qsizetype dropped = frames.size() - accepted;
    if (dropped > 0) {
        m_overrunFrames += quint64(dropped);
        m_runtime.noteTruthLoss(quint64(dropped));
        emit errorOccurred(QStringLiteral("Analysis queue overrun: %1 CAN_RX frames").arg(dropped));
    }

    m_maxQueuedFrames = std::max<quint64>(m_maxQueuedFrames, quint64(m_queue.size()));
    schedulePump();
    emitStatus(dropped > 0);
    emit handoffDrained();
}

void AnalysisWorkerRuntime::noteTruthLoss(quint64 frames, const QString& reason) {
    if (frames == 0) return;
    m_overrunFrames += frames;
    m_runtime.noteTruthLoss(frames);
    emit errorOccurred(reason.isEmpty()
                           ? QStringLiteral("Analysis queue overrun: %1 CAN_RX frames").arg(frames)
                           : reason);
    emitStatus(true);
}

void AnalysisWorkerRuntime::forceSnapshot() {
    emitSnapshot(true);
    emitStatus(true);
}

void AnalysisWorkerRuntime::pump() {
    m_pumpScheduled = false;
    if (m_queue.empty()) {
        emitStatus(false);
        return;
    }

    QElapsedTimer timer;
    timer.start();
    int processed = 0;
    FrameRecordList batch;
    batch.reserve(std::min<int>(kAnalysisPumpMaxFrames, int(m_queue.size())));

    while (!m_queue.empty() && processed < kAnalysisPumpMaxFrames) {
        const FrameRecord frame = m_queue.front();
        m_queue.pop_front();
        m_latestUs = std::max(m_latestUs, frame.tExtUs);
        batch.push_back(frame);
        ++processed;
        if (processed >= 64 && timer.elapsed() >= kAnalysisPumpBudgetMs) break;
    }

    if (!batch.isEmpty()) {
        m_runtime.ingestFrames(batch, QStringLiteral("live"));
        m_processedFrames += quint64(batch.size());
        ++m_pumpCount;
        m_pumpMaxMs = std::max<quint64>(m_pumpMaxMs, quint64(timer.elapsed()));
    }

    emitSnapshot(false);
    emitStatus(false);
    if (!m_queue.empty()) schedulePump();
}

void AnalysisWorkerRuntime::schedulePump() {
    if (m_pumpScheduled) return;
    m_pumpScheduled = true;
    QTimer::singleShot(0, this, &AnalysisWorkerRuntime::pump);
}

void AnalysisWorkerRuntime::emitStatus(bool force) {
    if (!force && m_statusClock.isValid() && m_statusClock.elapsed() < kAnalysisStatusIntervalMs) {
        if (!m_statusEmitScheduled) {
            m_statusEmitScheduled = true;
            const qint64 remainingMs = std::max<qint64>(1, kAnalysisStatusIntervalMs - m_statusClock.elapsed());
            QTimer::singleShot(int(remainingMs), this, [this]() {
                m_statusEmitScheduled = false;
                emitStatus(true);
            });
        }
        return;
    }
    m_statusEmitScheduled = false;
    m_statusClock.restart();
    const auto runtimeStatus = m_runtime.status();
    emit statusChanged(quint64(m_queue.size()),
                       m_maxQueuedFrames,
                       quint64(m_capacityFrames),
                       m_enqueuedFrames,
                       m_processedFrames,
                       m_overrunFrames,
                       m_pumpCount,
                       m_pumpMaxMs,
                       m_snapshotMaxMs,
                       runtimeStatus.truthLoss);
}

void AnalysisWorkerRuntime::emitSnapshot(bool force) {
    if (!force && m_snapshotClock.isValid() && m_snapshotClock.elapsed() < kAnalysisSnapshotIntervalMs) return;

    QElapsedTimer timer;
    timer.start();
    const qint64 nowMs = qint64(m_latestUs / 1000ULL);
    const auto snapshot = m_runtime.makeSnapshot(nowMs, QStringLiteral("live"));
    m_snapshotMaxMs = std::max<quint64>(m_snapshotMaxMs, quint64(timer.elapsed()));
    const QString level = snapshot.summary.value(QStringLiteral("level")).toString().isEmpty()
        ? QStringLiteral("OK")
        : snapshot.summary.value(QStringLiteral("level")).toString();
    emit snapshotReady(QStringLiteral("live"),
                       level,
                       snapshot.summary.value(QStringLiteral("text")).toString(),
                       diagnosticsWithQueueStatus(snapshot),
                       rowsToVariantList(snapshot.timingRows),
                       rowsToVariantList(snapshot.valueRows),
                       rowsToVariantList(snapshot.alarmRows));
    m_snapshotClock.restart();
}

QVariantList AnalysisWorkerRuntime::diagnosticsWithQueueStatus(const AnalysisRuntime::Snapshot& snapshot) const {
    QVariantList rows = snapshot.diagnostics;
    QVariantMap row;
    row.insert(QStringLiteral("key"), QStringLiteral("analysis_queue"));
    row.insert(QStringLiteral("level"), m_overrunFrames > 0 ? QStringLiteral("ERR") : QStringLiteral("OK"));
    row.insert(QStringLiteral("title"), QStringLiteral("analysis queue"));
    row.insert(QStringLiteral("value"), QStringLiteral("%1 / %2").arg(m_queue.size()).arg(m_capacityFrames));
    row.insert(QStringLiteral("detail"),
               QStringLiteral("max %1 enqueued %2 processed %3 overrun %4 pump_max_ms %5 snapshot_max_ms %6")
                   .arg(m_maxQueuedFrames)
                   .arg(m_enqueuedFrames)
                   .arg(m_processedFrames)
                   .arg(m_overrunFrames)
                   .arg(m_pumpMaxMs)
                   .arg(m_snapshotMaxMs));
    rows.push_back(row);
    return rows;
}

QVariantList AnalysisWorkerRuntime::rowsToVariantList(const QVector<QVariantMap>& rows) {
    QVariantList out;
    out.reserve(rows.size());
    for (const QVariantMap& row : rows) out.push_back(row);
    return out;
}

} // namespace CanMonitorAnalysis
