#pragma once

#include "analysis/AnalysisRuntime.h"
#include "transport/CoreDataBatches.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <deque>

namespace CanMonitorAnalysis {

class AnalysisWorkerRuntime : public QObject {
    Q_OBJECT
public:
    struct Status {
        quint64 queuedFrames = 0;
        quint64 maxQueuedFrames = 0;
        quint64 capacityFrames = 0;
        quint64 enqueuedFrames = 0;
        quint64 processedFrames = 0;
        quint64 overrunFrames = 0;
        quint64 pumpCount = 0;
        quint64 pumpMaxMs = 0;
        quint64 snapshotMaxMs = 0;
        quint64 truthLoss = 0;
    };

    explicit AnalysisWorkerRuntime(qsizetype capacityFrames = 131072, QObject* parent = nullptr);

public slots:
    void reset();
    void setConfig(const CanMonitorAnalysis::AnalysisRuntime::Config& config);
    void enqueueFrames(FrameRecordList frames);
    void enqueueCanRxFrames(QVector<CanMonitorTransport::CanRxLite> frames);
    void noteTruthLoss(quint64 frames, const QString& reason);
    void forceSnapshot();

signals:
    void snapshotReady(const QString& source,
                       const QString& level,
                       const QString& summary,
                       const QVariantList& diagnostics,
                       const QVariantList& timingRows,
                       const QVariantList& valueRows,
                       const QVariantList& alarmRows);
    void statusChanged(quint64 queuedFrames,
                       quint64 maxQueuedFrames,
                       quint64 capacityFrames,
                       quint64 enqueuedFrames,
                       quint64 processedFrames,
                       quint64 overrunFrames,
                       quint64 pumpCount,
                       quint64 pumpMaxMs,
                       quint64 snapshotMaxMs,
                       quint64 truthLoss);
    void errorOccurred(const QString& message);
    void handoffDrained();

private slots:
    void pump();

private:
    void schedulePump();
    void emitStatus(bool force = false);
    void emitSnapshot(bool force = false);
    QVariantList diagnosticsWithQueueStatus(const CanMonitorAnalysis::AnalysisRuntime::Snapshot& snapshot) const;
    static QVariantList rowsToVariantList(const QVector<QVariantMap>& rows);

    CanMonitorAnalysis::AnalysisRuntime m_runtime;
    std::deque<CanMonitorTransport::CanRxLite> m_queue;
    qsizetype m_capacityFrames = 0;
    bool m_pumpScheduled = false;
    bool m_statusEmitScheduled = false;
    quint64 m_enqueuedFrames = 0;
    quint64 m_processedFrames = 0;
    quint64 m_overrunFrames = 0;
    quint64 m_maxQueuedFrames = 0;
    quint64 m_pumpCount = 0;
    quint64 m_pumpMaxMs = 0;
    quint64 m_snapshotMaxMs = 0;
    quint64 m_latestUs = 0;
    QElapsedTimer m_statusClock;
    QElapsedTimer m_snapshotClock;
};

} // namespace CanMonitorAnalysis
