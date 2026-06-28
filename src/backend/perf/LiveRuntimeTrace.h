#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <array>
#include <atomic>

namespace CanMonitorPerf {

enum class LiveTraceSignal : int {
    FramesReceived = 0,
    TypedTruthStatusChanged,
    TypedProjectionStatusChanged,
    TypedTransportStatusChanged,
    TransportDiagnosticsChanged,
    AnalysisRuntimeSnapshotChanged,
    RawLedgerBatchCommitted,
    Count
};

struct RuntimeOwnerSnapshot {
    quint64 liveModelRows = 0;
    quint64 rawLedgerRows = 0;
    quint64 rawLedgerVisibleRows = 0;
    quint64 rawLedgerCacheRows = 0;
    quint64 graphSeriesCount = 0;
    quint64 graphSelectedKeys = 0;
    quint64 liveGraphSeries = 0;
    quint64 liveGraphPointsEstimate = 0;
    quint64 pendingLiveRows = 0;
    quint64 pendingLiveViewRows = 0;
    quint64 analysisTimingRows = 0;
    quint64 analysisValueRows = 0;
    quint64 analysisAlarmRows = 0;
    quint64 diagnosticsRows = 0;
    quint64 captureWriterQueueBytes = 0;
    quint64 captureWriterMaxQueueBytes = 0;
    quint64 captureWriterOverrunBytes = 0;
    quint64 rawLedgerWriterQueueBytes = 0;
    quint64 rawLedgerWriterMaxQueueBytes = 0;
    quint64 rawLedgerWriterOverrunBytes = 0;
    quint64 analysisQueueFrames = 0;
    quint64 analysisQueueMaxFrames = 0;
    quint64 analysisQueueOverrunFrames = 0;

    QJsonObject toJson() const;
};

class LiveRuntimeTraceRegistry {
public:
    struct SignalSnapshot {
        QString name;
        quint64 emitSeq = 0;
        quint64 slotSeq = 0;
        quint64 inflight = 0;
        qint64 lastEmitWallMs = 0;
        qint64 lastSlotWallMs = 0;
        qint64 lastSlotDelayMs = -1;
        qint64 maxSlotDelayMs = 0;
        quint64 payloadCountEst = 0;
        quint64 payloadBytesEst = 0;
    };

    struct HeartbeatSnapshot {
        quint64 sent = 0;
        quint64 ack = 0;
        bool inflight = false;
        qint64 pendingMs = 0;
        qint64 lastDelayMs = 0;
        qint64 maxDelayMs = 0;
        qint64 p95DelayMs = 0;
        quint64 stall100Ms = 0;
        quint64 stall500Ms = 0;
        quint64 stall1000Ms = 0;
    };

    static LiveRuntimeTraceRegistry& instance();

    void setEnabled(bool enabled);
    bool enabled() const;
    void reset();
    void noteEmit(LiveTraceSignal signal, quint64 payloadCount = 0, quint64 payloadBytes = 0);
    void noteSlot(LiveTraceSignal signal, quint64 payloadCount = 0, quint64 payloadBytes = 0);
    void noteHeartbeatSent(qint64 sentWallMs);
    void noteHeartbeatAck(qint64 sentWallMs);
    void noteHeartbeatPending(qint64 pendingMs);

    QVector<SignalSnapshot> signalSnapshots() const;
    HeartbeatSnapshot heartbeatSnapshot() const;
    QJsonObject toJson(qint64 nowWallMs) const;

    static const char* signalName(LiveTraceSignal signal);

private:
    LiveRuntimeTraceRegistry() = default;

    struct SignalCounters {
        std::atomic<quint64> emitSeq{0};
        std::atomic<quint64> slotSeq{0};
        std::atomic<qint64> lastEmitWallMs{0};
        std::atomic<qint64> lastSlotWallMs{0};
        std::atomic<qint64> lastSlotDelayMs{-1};
        std::atomic<qint64> maxSlotDelayMs{0};
        std::atomic<quint64> payloadCountEst{0};
        std::atomic<quint64> payloadBytesEst{0};
    };

    struct HeartbeatCounters {
        std::atomic<quint64> sent{0};
        std::atomic<quint64> ack{0};
        std::atomic<bool> inflight{false};
        std::atomic<qint64> pendingMs{0};
        std::atomic<qint64> lastDelayMs{0};
        std::atomic<qint64> maxDelayMs{0};
        std::atomic<quint64> stall100Ms{0};
        std::atomic<quint64> stall500Ms{0};
        std::atomic<quint64> stall1000Ms{0};
        mutable std::array<std::atomic<qint64>, 256> recent{};
        std::atomic<quint64> recentCount{0};
        std::atomic<quint64> recentCursor{0};
    };

    std::array<SignalCounters, static_cast<int>(LiveTraceSignal::Count)> m_signals;
    HeartbeatCounters m_heartbeat;
    std::atomic<bool> m_enabled{false};
};

class LiveRuntimeTraceService : public QObject {
    Q_OBJECT
public:
    explicit LiveRuntimeTraceService(QObject* parent = nullptr);
    ~LiveRuntimeTraceService() override;

    void startSession(const QString& directory, QObject* mainThreadTarget, bool resetCounters = true);
    void stopSession(const QString& reason = QString());
    void flush();
    void updateOwnerSnapshot(const RuntimeOwnerSnapshot& snapshot);
    QString sessionDirectory() const;

private:
    class Writer;

    QThread m_thread;
    Writer* m_writer = nullptr;
};

} // namespace CanMonitorPerf
