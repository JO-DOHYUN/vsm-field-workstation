#pragma once

#include "../AnalysisTypes.h"
#include "../CanTypes.h"
#include "../ModelPack.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace CanMonitorAnalysis {

class AnalysisRuntime {
public:
    using Key = quint64;

    struct Config {
        bool modelEnabled = false;
        int maxStateKeys = 4096;
        int maxRowsPerSnapshot = 1200;
        QHash<quint32, CanModel::RuleSpec> rules;
        QHash<quint32, CanModel::SignalMessageSpec> signalMessages;
    };

    struct Status {
        quint64 acceptedCanRxFrames = 0;
        quint64 decodedCanRxFrames = 0;
        quint64 truthLoss = 0;
        quint64 analysisOverrun = 0;
        quint64 captureSeqGapEvents = 0;
        quint64 captureSeqReorderEvents = 0;
        quint64 transportContaminatedIntervals = 0;
        quint64 snapshotCount = 0;
        int stateKeyCount = 0;
        int maxStateKeyCount = 0;
        int timingRows = 0;
        int valueRows = 0;
        int alarmRows = 0;
        int maxStateKeys = 0;
        int maxRowsPerSnapshot = 0;
    };

    struct Snapshot {
        quint64 seq = 0;
        qint64 nowMs = 0;
        QString source;
        Status status;
        QVariantMap summary;
        QVariantList diagnostics;
        QVector<QVariantMap> timingRows;
        QVector<QVariantMap> valueRows;
        QVector<QVariantMap> alarmRows;
    };

    struct Diff {
        quint64 fromSeq = 0;
        quint64 toSeq = 0;
        QStringList insertedKeys;
        QStringList removedKeys;
        QStringList changedKeys;
        bool summaryChanged = false;
        bool fullRefresh = false;
    };

    void reset();
    void setConfig(const Config& config);
    Config config() const { return m_config; }

    void noteTruthLoss(quint64 frames);
    void ingestFrame(const FrameRecord& frame, const QString& source);
    void ingestFrames(const FrameRecordList& frames, const QString& source);

    Snapshot makeSnapshot(qint64 nowMs, const QString& source);
    Status status() const;

    static Diff diff(const Snapshot& before, const Snapshot& after);
    static Key keyForFrame(const FrameRecord& frame);
    static Key ruleOnlyKey(quint32 canId);
    static QString keyText(Key key);
    static quint8 busFromKey(Key key);
    static quint32 canIdFromKey(Key key);
    static bool extFromKey(Key key);
    static bool rtrFromKey(Key key);

private:
    struct State {
        bool seen = false;
        FrameRecord lastFrame;
        QString lastSource;
        qint64 lastLocalSeenMs = -1;
        quint64 lastBoardSeenUs = 0;
        double lastGapMs = -1.0;
        double minGapMs = -1.0;
        double maxGapMs = -1.0;
        double lastTransportGapMs = -1.0;
        double maxTransportGapMs = -1.0;
        quint64 frameCount = 0;
        quint64 lastTransportGapEpoch = 0;
        quint64 transportContaminatedGapCount = 0;
        quint64 payloadFingerprint = 0;
        quint64 renderedFingerprint = 0;
        quint64 timingEventCount = 0;
        quint64 valueAlarmEventCount = 0;
        QString lastSeverity;
        QString lastReason;
        QString activeTimingAlarmKey;
        QString activeValueAlarmKey;
        QStringList timingEvents;
        int dlcHistogram[16] = {0};
    };

    QVariantMap makeTimingRow(Key key, const State* state, qint64 nowMs) const;
    QVariantMap makeValueRow(Key key, const State& state, qint64 nowMs) const;
    QVariantMap makeAlarmRow(const QString& key,
                             quint32 canId,
                             const QString& source,
                             const QString& severity,
                             const QString& name,
                             const QString& message,
                             const QString& category,
                             quint64 eventCount,
                             const QString& metricText,
                             double gaugePct) const;
    QVariantMap makeSummary(const Snapshot& snapshot) const;
    QVariantList makeDiagnostics(const Snapshot& snapshot) const;

    QString displayNameForId(quint32 id) const;
    QString idTextForKey(Key key, const FrameRecord* frame = nullptr) const;
    bool hasAlarmCapableSignals(quint32 id) const;
    static QString severityColor(const QString& severity);
    static int severityRank(const QString& severity);
    static QString fmtMs(double ms);
    static QString fmtPct(double pct);
    static quint64 framePayloadFingerprint(const FrameRecord& frame);
    static quint64 rowFingerprint(const QVariantMap& row);
    static QString busTextForFrame(const FrameRecord& frame);
    static void appendDlcHistogram(QVariantMap& row, const State& state);
    bool noteCaptureSeq(quint64 captureSeq);

    Config m_config;
    QHash<Key, State> m_states;
    QHash<quint32, bool> m_alarmCapableIds;
    Status m_status;
    quint64 m_nextSnapshotSeq = 1;
    bool m_hasMaxCaptureSeq = false;
    quint64 m_maxCaptureSeq = 0;
    bool m_captureSeqContinuityInitialized = false;
    quint64 m_nextExpectedCaptureSeq = 0;
    QSet<quint64> m_pendingCaptureSeq;
    quint64 m_transportGapEpoch = 0;
};

} // namespace CanMonitorAnalysis
