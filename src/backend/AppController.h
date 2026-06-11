#pragma once

#include "DetailListModel.h"
#include "FrameFilterProxyModel.h"
#include "FrameListModel.h"
#include "StableMapListModel.h"
#include "ReplayEngine.h"
#include "ReplayRuntime.h"
#include "SerialWorker.h"
#include "ModelPack.h"
#include "SessionManager.h"
#include "AnalysisTypes.h"
#include "SignalDecoder.h"
#include "AlarmManager.h"
#include "ControlCommandEncoder.h"
#include "control/ControlAuditModel.h"
#include "control/ControlRuntime.h"
#include "evidence/EvidenceRuntime.h"
#include "evidence/BusRoleResolver.h"
#include "transport/TransportRuntime.h"
#include "transport/TransportSession.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QMultiHash>
#include <QSet>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include <limits>

struct GraphBucketPoint {
    double tMs = 0.0;
    double minV = 0.0;
    double maxV = 0.0;
    double closeV = 0.0;
};

class TypedReplayReader;

struct GraphBucketCachePoint {
    quint64 bucketIndex = 0;
    quint64 closeUs = 0;
    double minV = 0.0;
    double maxV = 0.0;
    double closeV = 0.0;
};

class AppController : public QObject {
    Q_OBJECT
    friend class AppControllerLogFlowTest;
    friend class AppControllerAnalysisSourceFlowTest;
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY availablePortsChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString transportMode READ transportMode WRITE setTransportMode NOTIFY transportModeChanged)
    Q_PROPERTY(QString transportModeText READ transportModeText NOTIFY transportModeChanged)
    Q_PROPERTY(QString typedEvidenceSummary READ typedEvidenceSummary NOTIFY typedEvidenceChanged)
    Q_PROPERTY(QString typedCanSummary READ typedCanSummary NOTIFY typedEvidenceChanged)
    Q_PROPERTY(qulonglong typedRecordCount READ typedRecordCount NOTIFY typedEvidenceChanged)
    Q_PROPERTY(qulonglong typedTransportFaultCount READ typedTransportFaultCount NOTIFY typedEvidenceChanged)
    Q_PROPERTY(QString transportDiagnosticsLevel READ transportDiagnosticsLevel NOTIFY transportDiagnosticsChanged)
    Q_PROPERTY(QString transportDiagnosticsSummary READ transportDiagnosticsSummary NOTIFY transportDiagnosticsChanged)
    Q_PROPERTY(QVariantList transportDiagnostics READ transportDiagnostics NOTIFY transportDiagnosticsChanged)
    Q_PROPERTY(bool boardAlive READ boardAlive NOTIFY typedEvidenceChanged)
    Q_PROPERTY(QString boardConnectionSummary READ boardConnectionSummary NOTIFY typedEvidenceChanged)
    Q_PROPERTY(bool controlArmed READ controlArmed WRITE setControlArmed NOTIFY controlStateChanged)
    Q_PROPERTY(bool controlTestRunning READ controlTestRunning NOTIFY controlStateChanged)
    Q_PROPERTY(int controlTargetBus READ controlTargetBus WRITE setControlTargetBus NOTIFY controlStateChanged)
    Q_PROPERTY(int controlTargetRpm READ controlTargetRpm WRITE setControlTargetRpm NOTIFY controlStateChanged)
    Q_PROPERTY(double controlTargetSteeringDeg READ controlTargetSteeringDeg WRITE setControlTargetSteeringDeg NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlStatusSummary READ controlStatusSummary NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlBusSummary READ controlBusSummary NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlPolicySummary READ controlPolicySummary NOTIFY controlStateChanged)
    Q_PROPERTY(bool controlReady READ controlReady NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlBlockReason READ controlBlockReason NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlOperatorSummary READ controlOperatorSummary NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlActionVerdict READ controlActionVerdict NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlLastCommandSummary READ controlLastCommandSummary NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlLastWriteSummary READ controlLastWriteSummary NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlLastAckSummary READ controlLastAckSummary NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlLastAuditSummary READ controlLastAuditSummary NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlLastFeedbackSummary READ controlLastFeedbackSummary NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlLastFaultSummary READ controlLastFaultSummary NOTIFY controlStateChanged)
    Q_PROPERTY(bool controlActualTxConfirmed READ controlActualTxConfirmed NOTIFY controlStateChanged)
    Q_PROPERTY(bool controlFaultActive READ controlFaultActive NOTIFY controlStateChanged)
    Q_PROPERTY(QString controlEvidenceStatsSummary READ controlEvidenceStatsSummary NOTIFY controlStateChanged)
    Q_PROPERTY(QVariantList controlEvidenceStages READ controlEvidenceStages NOTIFY controlStateChanged)
    Q_PROPERTY(QVariantList controlOperatorChecklist READ controlOperatorChecklist NOTIFY controlStateChanged)
    Q_PROPERTY(QVariantList controlPolicyChecklist READ controlPolicyChecklist NOTIFY controlStateChanged)
    Q_PROPERTY(StableMapListModel* controlEvidenceModel READ controlEvidenceModel CONSTANT)
    Q_PROPERTY(QString rulesPath READ rulesPath WRITE setRulesPath NOTIFY rulesPathChanged)
    Q_PROPERTY(QString modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)
    Q_PROPERTY(QString logPath READ logPath NOTIFY logPathChanged)
    Q_PROPERTY(bool logRecordingActive READ logRecordingActive NOTIFY logStateChanged)
    Q_PROPERTY(bool logPendingSave READ logPendingSave NOTIFY logStateChanged)
    Q_PROPERTY(bool logStopping READ logStopping NOTIFY logStateChanged)
    Q_PROPERTY(bool logSaving READ logSaving NOTIFY logStateChanged)
    Q_PROPERTY(QString logPhaseText READ logPhaseText NOTIFY logStateChanged)
    Q_PROPERTY(QString logActionText READ logActionText NOTIFY logStateChanged)
    Q_PROPERTY(QString logStatusSummary READ logStatusSummary NOTIFY logStateChanged)
    Q_PROPERTY(QString loggingStateLevel READ loggingStateLevel NOTIFY logStateChanged)
    Q_PROPERTY(QString loggingStateText READ loggingStateText NOTIFY logStateChanged)
    Q_PROPERTY(QString suggestedLogSavePath READ suggestedLogSavePath NOTIFY logStateChanged)
    Q_PROPERTY(QString logTargetDirectory READ logTargetDirectory WRITE setLogTargetDirectory NOTIFY logTargetChanged)
    Q_PROPERTY(QString logTargetName READ logTargetName WRITE setLogTargetName NOTIFY logTargetChanged)
    Q_PROPERTY(QString logTargetPreview READ logTargetPreview NOTIFY logTargetChanged)
    Q_PROPERTY(qulonglong logRecordedBytes READ logRecordedBytes NOTIFY logStateChanged)
    Q_PROPERTY(qulonglong logRecordedFrameCount READ logRecordedFrameCount NOTIFY logStateChanged)
    Q_PROPERTY(QString replayPath READ replayPath NOTIFY replayPathChanged)
    Q_PROPERTY(QString replayOpenDirectory READ replayOpenDirectory NOTIFY replayPathChanged)
    Q_PROPERTY(bool replayLoaded READ replayLoaded NOTIFY replayStateChanged)
    Q_PROPERTY(bool replayPlaying READ replayPlaying NOTIFY replayStateChanged)
    Q_PROPERTY(int replayFrameCount READ replayFrameCount NOTIFY replayStateChanged)
    Q_PROPERTY(int replayCurrentIndex READ replayCurrentIndex NOTIFY replayStateChanged)
    Q_PROPERTY(double replayProgress READ replayProgress NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayCurrentTimeText READ replayCurrentTimeText NOTIFY replayStateChanged)
    Q_PROPERTY(bool replayRebuilding READ replayRebuilding NOTIFY replayStateChanged)
    Q_PROPERTY(double replayTargetProgress READ replayTargetProgress NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayTargetTimeText READ replayTargetTimeText NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayDurationText READ replayDurationText NOTIFY replayStateChanged)
    Q_PROPERTY(double replaySpeed READ replaySpeed NOTIFY replayStateChanged)
    Q_PROPERTY(bool replayLoop READ replayLoop NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayTypedCaptureState READ replayTypedCaptureState NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayTypedDiagnosticsSummary READ replayTypedDiagnosticsSummary NOTIFY replayStateChanged)
    Q_PROPERTY(QVariantList replayTypedDiagnostics READ replayTypedDiagnostics NOTIFY replayStateChanged)
    Q_PROPERTY(int liveRxFps READ liveRxFps NOTIFY liveStatsChanged)
    Q_PROPERTY(int liveTxFps READ liveTxFps NOTIFY liveStatsChanged)
    Q_PROPERTY(quint32 droppedTotal READ droppedTotal NOTIFY liveStatsChanged)
    Q_PROPERTY(quint32 fifoOverflowTotal READ fifoOverflowTotal NOTIFY liveStatsChanged)
    Q_PROPERTY(int errPassiveCount READ errPassiveCount NOTIFY liveStatsChanged)
    Q_PROPERTY(int busOffCount READ busOffCount NOTIFY liveStatsChanged)
    Q_PROPERTY(QString liveStatsSummary READ liveStatsSummary NOTIFY liveStatsChanged)
    Q_PROPERTY(bool liveUiPaused READ liveUiPaused NOTIFY liveUiPausedChanged)
    Q_PROPERTY(QString analysisSourceText READ analysisSourceText NOTIFY derivedSummaryChanged)
    Q_PROPERTY(bool replayAnalysisActive READ replayAnalysisActive NOTIFY derivedSummaryChanged)
    Q_PROPERTY(bool replayAnalysisHeld READ replayAnalysisHeld NOTIFY replayStateChanged)
    Q_PROPERTY(bool modelActive READ modelActive NOTIFY rulesChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY rulesChanged)
    Q_PROPERTY(QString modelKey READ modelKey NOTIFY rulesChanged)
    Q_PROPERTY(QString modelVersion READ modelVersion NOTIFY rulesChanged)
    Q_PROPERTY(QString modelVendor READ modelVendor NOTIFY rulesChanged)
    Q_PROPERTY(QString modelSchema READ modelSchema NOTIFY rulesChanged)
    Q_PROPERTY(QString modelNotes READ modelNotes NOTIFY rulesChanged)
    Q_PROPERTY(QString modelSummary READ modelSummary NOTIFY rulesChanged)
    Q_PROPERTY(QString modelSourceSummary READ modelSourceSummary NOTIFY rulesChanged)
    Q_PROPERTY(bool rulesLoaded READ rulesLoaded NOTIFY rulesChanged)
    Q_PROPERTY(int rulesCount READ rulesCount NOTIFY rulesChanged)
    Q_PROPERTY(QString rulesSummary READ rulesSummary NOTIFY rulesChanged)
    Q_PROPERTY(QString rulesSourceSummary READ rulesSourceSummary NOTIFY rulesChanged)
    Q_PROPERTY(bool signalDbLoaded READ signalDbLoaded NOTIFY signalDbChanged)
    Q_PROPERTY(int signalDbMessageCount READ signalDbMessageCount NOTIFY signalDbChanged)
    Q_PROPERTY(QString signalDbSummary READ signalDbSummary NOTIFY signalDbChanged)
    Q_PROPERTY(QString selectedValueId READ selectedValueId NOTIFY selectedValueIdChanged)
    Q_PROPERTY(QString timingSortMode READ timingSortMode WRITE setTimingSortMode NOTIFY sortOptionsChanged)
    Q_PROPERTY(bool timingSortDescending READ timingSortDescending WRITE setTimingSortDescending NOTIFY sortOptionsChanged)
    Q_PROPERTY(QString valueSortMode READ valueSortMode WRITE setValueSortMode NOTIFY sortOptionsChanged)
    Q_PROPERTY(bool valueSortDescending READ valueSortDescending WRITE setValueSortDescending NOTIFY sortOptionsChanged)
    Q_PROPERTY(QString alarmSortMode READ alarmSortMode WRITE setAlarmSortMode NOTIFY sortOptionsChanged)
    Q_PROPERTY(bool alarmSortDescending READ alarmSortDescending WRITE setAlarmSortDescending NOTIFY sortOptionsChanged)
    Q_PROPERTY(QString timingFilterId READ timingFilterId WRITE setTimingFilterId NOTIFY filtersChanged)
    Q_PROPERTY(QString timingFilterSeverity READ timingFilterSeverity WRITE setTimingFilterSeverity NOTIFY filtersChanged)
    Q_PROPERTY(QString timingFilterName READ timingFilterName WRITE setTimingFilterName NOTIFY filtersChanged)
    Q_PROPERTY(QString timingFilterReason READ timingFilterReason WRITE setTimingFilterReason NOTIFY filtersChanged)
    Q_PROPERTY(QString timingFilterExpected READ timingFilterExpected WRITE setTimingFilterExpected NOTIFY filtersChanged)
    Q_PROPERTY(QString timingFilterGap READ timingFilterGap WRITE setTimingFilterGap NOTIFY filtersChanged)
    Q_PROPERTY(QString timingFilterAge READ timingFilterAge WRITE setTimingFilterAge NOTIFY filtersChanged)
    Q_PROPERTY(QString timingFilterSource READ timingFilterSource WRITE setTimingFilterSource NOTIFY filtersChanged)
    Q_PROPERTY(QString valueFilterId READ valueFilterId WRITE setValueFilterId NOTIFY filtersChanged)
    Q_PROPERTY(QString valueFilterSeverity READ valueFilterSeverity WRITE setValueFilterSeverity NOTIFY filtersChanged)
    Q_PROPERTY(QString valueFilterName READ valueFilterName WRITE setValueFilterName NOTIFY filtersChanged)
    Q_PROPERTY(QString valueFilterSource READ valueFilterSource WRITE setValueFilterSource NOTIFY filtersChanged)
    Q_PROPERTY(QString valueFilterRaw READ valueFilterRaw WRITE setValueFilterRaw NOTIFY filtersChanged)
    Q_PROPERTY(QString valueFilterGap READ valueFilterGap WRITE setValueFilterGap NOTIFY filtersChanged)
    Q_PROPERTY(QString valueFilterReason READ valueFilterReason WRITE setValueFilterReason NOTIFY filtersChanged)
    Q_PROPERTY(QString alarmFilterId READ alarmFilterId WRITE setAlarmFilterId NOTIFY filtersChanged)
    Q_PROPERTY(QString alarmFilterSeverity READ alarmFilterSeverity WRITE setAlarmFilterSeverity NOTIFY filtersChanged)
    Q_PROPERTY(QString alarmFilterTime READ alarmFilterTime WRITE setAlarmFilterTime NOTIFY filtersChanged)
    Q_PROPERTY(QString alarmFilterName READ alarmFilterName WRITE setAlarmFilterName NOTIFY filtersChanged)
    Q_PROPERTY(QString alarmFilterSource READ alarmFilterSource WRITE setAlarmFilterSource NOTIFY filtersChanged)
    Q_PROPERTY(QString alarmFilterMessage READ alarmFilterMessage WRITE setAlarmFilterMessage NOTIFY filtersChanged)
    Q_PROPERTY(QString alarmFilterText READ alarmFilterText WRITE setAlarmFilterText NOTIFY filtersChanged)
    Q_PROPERTY(StableMapListModel* timingModel READ timingModel CONSTANT)
    Q_PROPERTY(StableMapListModel* valueModel READ valueModel CONSTANT)
    Q_PROPERTY(StableMapListModel* alarmModel READ alarmModel CONSTANT)
    Q_PROPERTY(bool timingViewHeld READ timingViewHeld NOTIFY viewHoldChanged)
    Q_PROPERTY(bool valueViewHeld READ valueViewHeld NOTIFY viewHoldChanged)
    Q_PROPERTY(bool alarmViewHeld READ alarmViewHeld NOTIFY viewHoldChanged)
    Q_PROPERTY(DetailListModel* valueDetailModel READ valueDetailModel CONSTANT)
    Q_PROPERTY(FrameListModel* recentFrames READ recentFrames CONSTANT)
    Q_PROPERTY(FrameListModel* liveFrames READ liveFrames CONSTANT)
    Q_PROPERTY(FrameListModel* replayFrames READ replayFrames CONSTANT)
    Q_PROPERTY(FrameFilterProxyModel* liveFrameView READ liveFrameView CONSTANT)
    Q_PROPERTY(FrameFilterProxyModel* replayFrameView READ replayFrameView CONSTANT)
    Q_PROPERTY(int timingIssueCount READ timingIssueCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int valueIssueCount READ valueIssueCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int activeAlarmCount READ activeAlarmCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int timingCumulativeCount READ timingCumulativeCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int valueCumulativeCount READ valueCumulativeCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int alarmCumulativeCount READ alarmCumulativeCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int timingWarnCount READ timingWarnCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int timingErrCount READ timingErrCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int valueWarnCount READ valueWarnCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int valueErrCount READ valueErrCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int alarmWarnCount READ alarmWarnCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int alarmErrCount READ alarmErrCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString timingLevel READ timingLevel NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString valueLevel READ valueLevel NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString alarmLevel READ alarmLevel NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString systemLevel READ systemLevel NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString topTimingSummary READ topTimingSummary NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString topValueSummary READ topValueSummary NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString topAlarmSummary READ topAlarmSummary NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString topTimingId READ topTimingId NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString topValueId READ topValueId NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString topAlarmId READ topAlarmId NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString sessionSummary READ sessionSummary NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString sessionUptimeText READ sessionUptimeText NOTIFY operatorRecentEventsChanged)
    Q_PROPERTY(QVariantList operatorRecentEvents READ operatorRecentEvents NOTIFY operatorRecentEventsChanged)
    Q_PROPERTY(QString operatorRecentSummary READ operatorRecentSummary NOTIFY operatorRecentEventsChanged)
    Q_PROPERTY(QString analysisContextText READ analysisContextText NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString activeViewStateSummary READ activeViewStateSummary NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString replayCursorSummary READ replayCursorSummary NOTIFY replayStateChanged)
    Q_PROPERTY(QString replaySnapshotSummary READ replaySnapshotSummary NOTIFY replayStateChanged)
    Q_PROPERTY(int liveObservedIdCount READ liveObservedIdCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(int replayObservedIdCount READ replayObservedIdCount NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QVariantList replayIssueMarkers READ replayIssueMarkers NOTIFY replayIssueMarkersChanged)
    Q_PROPERTY(QString replayIssueSummary READ replayIssueSummary NOTIFY replayIssueMarkersChanged)
    Q_PROPERTY(int replayTimingMarkerCount READ replayTimingMarkerCount NOTIFY replayIssueMarkersChanged)
    Q_PROPERTY(int replayValueMarkerCount READ replayValueMarkerCount NOTIFY replayIssueMarkersChanged)
    Q_PROPERTY(int replayAlarmMarkerCount READ replayAlarmMarkerCount NOTIFY replayIssueMarkersChanged)
    Q_PROPERTY(QString modelModeText READ modelModeText NOTIFY rulesChanged)
    Q_PROPERTY(QString busHealthLevel READ busHealthLevel NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString busHealthText READ busHealthText NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString analysisReliabilityText READ analysisReliabilityText NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString rootCauseSummary READ rootCauseSummary NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString operatorHeadline READ operatorHeadline NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString operatorActionLevel READ operatorActionLevel NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString operatorActionText READ operatorActionText NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString primaryIssueKind READ primaryIssueKind NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString primaryIssueId READ primaryIssueId NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString primaryIssueSummary READ primaryIssueSummary NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString primaryIssueTargetTab READ primaryIssueTargetTab NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString primaryIssueMarkerKind READ primaryIssueMarkerKind NOTIFY derivedSummaryChanged)
    Q_PROPERTY(bool primaryIssueSeekAvailable READ primaryIssueSeekAvailable NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString operatorFocusText READ operatorFocusText NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString sourceStateLevel READ sourceStateLevel NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString sourceStateText READ sourceStateText NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString analysisModeLevel READ analysisModeLevel NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString analysisModeText READ analysisModeText NOTIFY derivedSummaryChanged)
    Q_PROPERTY(QString modelDiagnosticsLevel READ modelDiagnosticsLevel NOTIFY rulesChanged)
    Q_PROPERTY(QString modelDiagnosticsSummary READ modelDiagnosticsSummary NOTIFY rulesChanged)
    Q_PROPERTY(int modelSignalCount READ modelSignalCount NOTIFY rulesChanged)
    Q_PROPERTY(int modelAlarmSignalCount READ modelAlarmSignalCount NOTIFY rulesChanged)
    Q_PROPERTY(int modelMonitorOnlySignalCount READ modelMonitorOnlySignalCount NOTIFY rulesChanged)
    Q_PROPERTY(int modelTimingRuleCount READ modelTimingRuleCount NOTIFY rulesChanged)
    Q_PROPERTY(QString sessionFilePath READ sessionFilePath NOTIFY rulesChanged)
    Q_PROPERTY(QString defaultLogDirectory READ defaultLogDirectory CONSTANT)
    Q_PROPERTY(QString defaultSnapshotDirectory READ defaultSnapshotDirectory CONSTANT)
    Q_PROPERTY(QString suggestedSnapshotPath READ suggestedSnapshotPath NOTIFY logTargetChanged)
    Q_PROPERTY(QVariantList graphCatalog READ graphCatalog NOTIFY graphCatalogChanged)
    Q_PROPERTY(StableMapListModel* graphCatalogModel READ graphCatalogModel CONSTANT)
    Q_PROPERTY(QVariantList graphPresets READ graphPresets NOTIFY graphCatalogChanged)
    Q_PROPERTY(QStringList graphSelectedKeys READ graphSelectedKeys NOTIFY graphSelectionChanged)
    Q_PROPERTY(QString graphPresetKey READ graphPresetKey NOTIFY graphSelectionChanged)
    Q_PROPERTY(QVariantList graphSeries READ graphSeries NOTIFY graphSeriesChanged)
    Q_PROPERTY(QString graphSourceSummary READ graphSourceSummary NOTIFY graphSeriesChanged)
    Q_PROPERTY(QString graphRangeSummary READ graphRangeSummary NOTIFY graphSeriesChanged)
    Q_PROPERTY(bool graphDetailZoom READ graphDetailZoom WRITE setGraphDetailZoom NOTIFY graphSelectionChanged)
    Q_PROPERTY(QString graphDetailZoomSummary READ graphDetailZoomSummary NOTIFY graphSeriesChanged)
    Q_PROPERTY(int graphWindowMs READ graphWindowMs WRITE setGraphWindowMs NOTIFY graphSelectionChanged)
    Q_PROPERTY(QVariantList graphOverviewSeries READ graphOverviewSeries NOTIFY graphOverviewChanged)
    Q_PROPERTY(QString graphOverviewSourceSummary READ graphOverviewSourceSummary NOTIFY graphOverviewChanged)
    Q_PROPERTY(QString graphOverviewRangeSummary READ graphOverviewRangeSummary NOTIFY graphOverviewChanged)
    Q_PROPERTY(bool graphOverviewBuilding READ graphOverviewBuilding NOTIFY graphOverviewChanged)
    Q_PROPERTY(double graphOverviewBuildProgress READ graphOverviewBuildProgress NOTIFY graphOverviewChanged)
    Q_PROPERTY(QString graphOverviewBuildText READ graphOverviewBuildText NOTIFY graphOverviewChanged)
    Q_PROPERTY(bool graphOverviewReady READ graphOverviewReady NOTIFY graphOverviewChanged)
    Q_PROPERTY(QString graphOverviewStartText READ graphOverviewStartText NOTIFY replayStateChanged)
    Q_PROPERTY(QString graphOverviewEndText READ graphOverviewEndText NOTIFY replayStateChanged)
    Q_PROPERTY(QString graphOverviewDurationText READ graphOverviewDurationText NOTIFY replayStateChanged)
    Q_PROPERTY(double graphOverviewCursorMs READ graphOverviewCursorMs NOTIFY replayStateChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    QStringList availablePorts() const { return m_availablePorts; }
    bool connected() const { return m_connected; }
    QString statusText() const { return m_statusText; }
    QString transportMode() const { return m_transportModeKey; }
    QString transportModeText() const;
    Q_INVOKABLE void setTransportMode(const QString& mode);
    QString typedEvidenceSummary() const;
    QString typedCanSummary() const;
    qulonglong typedRecordCount() const { return qulonglong(m_typedRecordCount); }
    qulonglong typedTransportFaultCount() const;
    QString transportDiagnosticsLevel() const { return m_transportSession.level(); }
    QString transportDiagnosticsSummary() const { return m_transportSession.summary(); }
    QVariantList transportDiagnostics() const { return m_transportSession.rows(); }
    bool boardAlive() const { return m_evidenceRuntime.boardAlive(); }
    QString boardConnectionSummary() const;
    bool controlArmed() const { return m_controlRuntime.armed(); }
    bool controlTestRunning() const { return m_controlRuntime.testRunning(); }
    int controlTargetBus() const { return m_controlRuntime.target().bus; }
    int controlTargetRpm() const { return m_controlRuntime.target().rpm; }
    double controlTargetSteeringDeg() const { return m_controlRuntime.target().steeringDeg; }
    QString controlStatusSummary() const;
    QString controlBusSummary() const { return m_controlBusSummary; }
    QString controlPolicySummary() const;
    bool controlReady() const;
    QString controlBlockReason() const;
    QString controlOperatorSummary() const;
    QString controlActionVerdict() const;
    QString controlLastCommandSummary() const { return m_controlRuntime.lastCommandSummary(); }
    QString controlLastWriteSummary() const { return m_controlAudit.lastHostWriteSummary(); }
    QString controlLastAckSummary() const { return m_controlAudit.lastAckSummary(); }
    QString controlLastAuditSummary() const { return m_controlAudit.lastAuditSummary(); }
    QString controlLastFeedbackSummary() const { return m_controlAudit.lastFeedbackSummary(); }
    QString controlLastFaultSummary() const { return m_controlAudit.lastFaultSummary(); }
    bool controlActualTxConfirmed() const { return m_controlAudit.actualTxConfirmed(); }
    bool controlFaultActive() const { return m_controlAudit.faultActive(); }
    QString controlEvidenceStatsSummary() const;
    QVariantList controlEvidenceStages() const;
    QVariantList controlOperatorChecklist() const;
    QVariantList controlPolicyChecklist() const;
    StableMapListModel* controlEvidenceModel() { return m_controlAudit.model(); }
    QString rulesPath() const { return m_rulesPath; }
    QString modelPath() const { return m_rulesPath; }
    void setRulesPath(const QString& path);
    void setModelPath(const QString& path) { setRulesPath(path); }
    QString logPath() const { return m_logPath; }
    QString replayPath() const { return m_replayPath; }
    QString replayOpenDirectory() const;
    bool logRecordingActive() const { return m_logRecordingActive; }
    bool logPendingSave() const { return m_logPendingSave; }
    bool logStopping() const { return m_logStopping; }
    bool logSaving() const { return m_logSaving; }
    QString logPhaseText() const;
    QString logActionText() const;
    QString logStatusSummary() const;
    QString loggingStateLevel() const;
    QString loggingStateText() const;
    QString suggestedLogSavePath() const { return m_logSuggestedSavePath; }
    QString logTargetDirectory() const;
    QString logTargetName() const { return m_logTargetName; }
    QString logTargetPreview() const;
    qulonglong logRecordedBytes() const { return qulonglong(m_logRecordedBytes); }
    qulonglong logRecordedFrameCount() const { return qulonglong(m_logRecordedFrameCount); }
    bool replayLoaded() const { return m_replayLoaded; }
    bool replayPlaying() const { return m_replayPlaying; }
    int replayFrameCount() const { return m_replayFrameCount; }
    int replayCurrentIndex() const { return m_replayCurrentIndex; }
    double replayProgress() const { return m_replayProgress; }
    QString replayCurrentTimeText() const;
    bool replayRebuilding() const { return m_replayRebuildActive; }
    double replayTargetProgress() const;
    QString replayTargetTimeText() const;
    QString replayDurationText() const;
    double replaySpeed() const { return m_replaySpeed; }
    bool replayLoop() const { return m_replayLoop; }
    QString replayTypedCaptureState() const { return m_replayTypedCaptureState; }
    QString replayTypedDiagnosticsSummary() const { return m_replayTypedDiagnosticsSummary; }
    QVariantList replayTypedDiagnostics() const { return m_replayTypedDiagnostics; }
    int liveRxFps() const { return int(m_lastStats.rxFps1s); }
    int liveTxFps() const { return int(m_lastStats.txFps1s); }
    quint32 droppedTotal() const { return m_lastStats.droppedTotal; }
    quint32 fifoOverflowTotal() const { return m_lastStats.fifoOverflowTotal; }
    int errPassiveCount() const { return int(m_lastStats.errPassive1s); }
    int busOffCount() const { return int(m_lastStats.busOff1s); }
    QString liveStatsSummary() const;
    bool liveUiPaused() const { return m_liveUiPaused; }
    QString analysisSourceText() const;
    bool replayAnalysisHeld() const { return m_replayAnalysisHeld; }
    bool modelActive() const { return m_modelEnabled; }
    QString modelName() const { return m_modelEnabled ? m_modelMeta.modelName : QStringLiteral("모델 해제"); }
    QString modelKey() const { return m_modelEnabled ? m_modelMeta.modelKey : QString(); }
    QString modelVersion() const { return m_modelEnabled ? m_modelMeta.modelVersion : QString(); }
    QString modelVendor() const { return m_modelEnabled ? m_modelMeta.vendor : QString(); }
    QString modelSchema() const { return m_modelEnabled ? m_modelMeta.schema : QString(); }
    QString modelNotes() const { return m_modelEnabled ? m_modelMeta.notes : QString(); }
    QString modelSummary() const;
    QString modelSourceSummary() const;
    bool rulesLoaded() const { return m_modelEnabled && !m_rules.isEmpty(); }
    int rulesCount() const { return m_rules.size(); }
    QString rulesSummary() const;
    QString rulesSourceSummary() const;
    bool signalDbLoaded() const { return m_modelEnabled && !m_signalMessages.isEmpty(); }
    int signalDbMessageCount() const { return m_signalMessages.size(); }
    QString signalDbSummary() const;
    QString selectedValueId() const;
    StableMapListModel* timingModel() { return &m_timingModel; }
    StableMapListModel* valueModel() { return &m_valueModel; }
    StableMapListModel* alarmModel() { return &m_alarmModel; }
    StableMapListModel* graphCatalogModel() { return &m_graphCatalogModel; }
    bool timingViewHeld() const { return m_timingViewHeld; }
    bool valueViewHeld() const { return m_valueViewHeld; }
    bool alarmViewHeld() const { return m_alarmViewHeld; }
    DetailListModel* valueDetailModel() { return &m_valueDetailModel; }
    QString timingSortMode() const { return m_timingSortMode; }
    bool timingSortDescending() const { return m_timingSortDescending; }
    QString valueSortMode() const { return m_valueSortMode; }
    bool valueSortDescending() const { return m_valueSortDescending; }
    QString alarmSortMode() const { return m_alarmSortMode; }
    bool alarmSortDescending() const { return m_alarmSortDescending; }
    QString timingFilterId() const { return m_timingFilterId; }
    QString timingFilterSeverity() const { return m_timingFilterSeverity; }
    QString timingFilterName() const { return m_timingFilterName; }
    QString timingFilterReason() const { return m_timingFilterReason; }
    QString timingFilterExpected() const { return m_timingFilterExpected; }
    QString timingFilterGap() const { return m_timingFilterGap; }
    QString timingFilterAge() const { return m_timingFilterAge; }
    QString timingFilterSource() const { return m_timingFilterSource; }
    QString valueFilterId() const { return m_valueFilterId; }
    QString valueFilterSeverity() const { return m_valueFilterSeverity; }
    QString valueFilterName() const { return m_valueFilterName; }
    QString valueFilterSource() const { return m_valueFilterSource; }
    QString valueFilterRaw() const { return m_valueFilterRaw; }
    QString valueFilterGap() const { return m_valueFilterGap; }
    QString valueFilterReason() const { return m_valueFilterReason; }
    QString alarmFilterId() const { return m_alarmFilterId; }
    QString alarmFilterSeverity() const { return m_alarmFilterSeverity; }
    QString alarmFilterTime() const { return m_alarmFilterTime; }
    QString alarmFilterName() const { return m_alarmFilterName; }
    QString alarmFilterSource() const { return m_alarmFilterSource; }
    QString alarmFilterMessage() const { return m_alarmFilterMessage; }
    QString alarmFilterText() const { return m_alarmFilterText; }
    FrameListModel* recentFrames() { return &m_recentFrames; }
    FrameListModel* liveFrames() { return &m_liveFrames; }
    FrameListModel* replayFrames() { return &m_replayFrames; }
    FrameFilterProxyModel* liveFrameView() { return &m_liveFrameView; }
    FrameFilterProxyModel* replayFrameView() { return &m_replayFrameView; }
    int timingIssueCount() const;
    int valueIssueCount() const;
    int activeAlarmCount() const;
    int timingCumulativeCount() const;
    int valueCumulativeCount() const;
    int alarmCumulativeCount() const;
    int timingWarnCount() const;
    int timingErrCount() const;
    int valueWarnCount() const;
    int valueErrCount() const;
    int alarmWarnCount() const;
    int alarmErrCount() const;
    QString timingLevel() const;
    QString valueLevel() const;
    QString alarmLevel() const;
    QString systemLevel() const;
    QString topTimingSummary() const;
    QString topValueSummary() const;
    QString topAlarmSummary() const;
    QString topTimingId() const;
    QString topValueId() const;
    QString topAlarmId() const;
    QString sessionSummary() const;
    QString sessionUptimeText() const;
    QVariantList operatorRecentEvents() const;
    QString operatorRecentSummary() const;
    QString analysisContextText() const;
    QString activeViewStateSummary() const;
    QString replayCursorSummary() const;
    QString replaySnapshotSummary() const;
    int liveObservedIdCount() const { return m_liveStates.size(); }
    int replayObservedIdCount() const;
    QVariantList replayIssueMarkers() const;
    QString replayIssueSummary() const;
    int replayTimingMarkerCount() const;
    int replayValueMarkerCount() const;
    int replayAlarmMarkerCount() const;
    QString modelModeText() const;
    QString busHealthLevel() const;
    QString busHealthText() const;
    QString analysisReliabilityText() const;
    QString rootCauseSummary() const;
    QString operatorHeadline() const;
    QString operatorActionLevel() const;
    QString operatorActionText() const;
    QString primaryIssueKind() const;
    QString primaryIssueId() const;
    QString primaryIssueSummary() const;
    QString primaryIssueTargetTab() const;
    QString primaryIssueMarkerKind() const;
    bool primaryIssueSeekAvailable() const;
    QString operatorFocusText() const;
    QString sourceStateLevel() const;
    QString sourceStateText() const;
    QString analysisModeLevel() const;
    QString analysisModeText() const;
    QString modelDiagnosticsLevel() const;
    QString modelDiagnosticsSummary() const;
    int modelSignalCount() const;
    int modelAlarmSignalCount() const;
    int modelMonitorOnlySignalCount() const;
    int modelTimingRuleCount() const;
    QString sessionFilePath() const;
    QString defaultLogDirectory() const;
    QString defaultSnapshotDirectory() const;
    QString suggestedSnapshotPath() const;
    QVariantList graphCatalog() const { return m_graphCatalogCache; }
    QVariantList graphPresets() const { return m_graphPresetCache; }
    QStringList graphSelectedKeys() const { return m_graphSelectedKeys; }
    QString graphPresetKey() const { return m_graphPresetKey; }
    QVariantList graphSeries() const { return m_graphSeriesCache; }
    QString graphSourceSummary() const { return m_graphSourceSummaryCache; }
    QString graphRangeSummary() const { return m_graphRangeSummaryCache; }
    bool graphDetailZoom() const { return m_graphDetailZoom; }
    QString graphDetailZoomSummary() const { return m_graphDetailZoomSummaryCache; }
    int graphWindowMs() const { return m_graphWindowMs; }

    QVariantList graphOverviewSeries() const { return m_graphOverviewSeriesCache; }
    QString graphOverviewSourceSummary() const { return m_graphOverviewSourceSummaryCache; }
    QString graphOverviewRangeSummary() const { return m_graphOverviewRangeSummaryCache; }
    bool graphOverviewBuilding() const { return m_graphOverviewBuildActive; }
    double graphOverviewBuildProgress() const { return m_graphOverviewBuildProgress; }
    QString graphOverviewBuildText() const { return m_graphOverviewBuildTextCache; }
    bool graphOverviewReady() const { return !m_graphOverviewSeriesCache.isEmpty() && !m_graphOverviewBuildActive; }
    QString graphOverviewStartText() const;
    QString graphOverviewEndText() const;
    QString graphOverviewDurationText() const;
    double graphOverviewCursorMs() const;

    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE void connectPort(const QString& portName);
    Q_INVOKABLE void disconnectPort();
    Q_INVOKABLE void startLog();
    Q_INVOKABLE void stopLog();
    Q_INVOKABLE void finalizePendingLogSave(const QString& filePath);
    Q_INVOKABLE void discardPendingLog();
    Q_INVOKABLE void setLogTargetDirectory(const QString& directory);
    Q_INVOKABLE void setLogTargetName(const QString& name);
    Q_INVOKABLE void loadReplay(const QString& filePath);
    Q_INVOKABLE void playReplay(double speed);
    Q_INVOKABLE void pauseReplay();
    Q_INVOKABLE void stopReplay();
    Q_INVOKABLE void useLiveAnalysis();
    Q_INVOKABLE void setReplayLoop(bool enabled);
    Q_INVOKABLE void seekReplay(double progress);
    Q_INVOKABLE void commitSeekReplay(double progress);
    Q_INVOKABLE QString replayTimeTextForProgress(double progress) const;
    Q_INVOKABLE void stepReplay(int delta);
    Q_INVOKABLE void exportAnalysisSnapshot(const QString& filePath);
    Q_INVOKABLE void useBundledModel();
    Q_INVOKABLE void clearModel();
    Q_INVOKABLE void clearFrames();
    Q_INVOKABLE void setTimingViewHeld(bool held);
    Q_INVOKABLE void setValueViewHeld(bool held);
    Q_INVOKABLE void setAlarmViewHeld(bool held);
    Q_INVOKABLE void setLiveUiPaused(bool paused);
    Q_INVOKABLE void toggleLiveUiPaused();
    Q_INVOKABLE void selectValueId(const QString& idText);
    Q_INVOKABLE void setTimingSortMode(const QString& mode);
    Q_INVOKABLE void setTimingSortDescending(bool descending);
    Q_INVOKABLE void setValueSortMode(const QString& mode);
    Q_INVOKABLE void setValueSortDescending(bool descending);
    Q_INVOKABLE void setAlarmSortMode(const QString& mode);
    Q_INVOKABLE void setAlarmSortDescending(bool descending);
    Q_INVOKABLE void toggleTimingSort(const QString& mode);
    Q_INVOKABLE void toggleValueSort(const QString& mode);
    Q_INVOKABLE void toggleAlarmSort(const QString& mode);
    Q_INVOKABLE void setTimingFilterId(const QString& text);
    Q_INVOKABLE void setTimingFilterSeverity(const QString& text);
    Q_INVOKABLE void setTimingFilterName(const QString& text);
    Q_INVOKABLE void setTimingFilterReason(const QString& text);
    Q_INVOKABLE void setTimingFilterExpected(const QString& text);
    Q_INVOKABLE void setTimingFilterGap(const QString& text);
    Q_INVOKABLE void setTimingFilterAge(const QString& text);
    Q_INVOKABLE void setTimingFilterSource(const QString& text);
    Q_INVOKABLE void setValueFilterId(const QString& text);
    Q_INVOKABLE void setValueFilterSeverity(const QString& text);
    Q_INVOKABLE void setValueFilterName(const QString& text);
    Q_INVOKABLE void setValueFilterSource(const QString& text);
    Q_INVOKABLE void setValueFilterRaw(const QString& text);
    Q_INVOKABLE void setValueFilterGap(const QString& text);
    Q_INVOKABLE void setValueFilterReason(const QString& text);
    Q_INVOKABLE void setAlarmFilterId(const QString& text);
    Q_INVOKABLE void setAlarmFilterSeverity(const QString& text);
    Q_INVOKABLE void setAlarmFilterTime(const QString& text);
    Q_INVOKABLE void setAlarmFilterName(const QString& text);
    Q_INVOKABLE void setAlarmFilterSource(const QString& text);
    Q_INVOKABLE void setAlarmFilterMessage(const QString& text);
    Q_INVOKABLE void setAlarmFilterText(const QString& text);
    Q_INVOKABLE void clearSavedSession();
    Q_INVOKABLE void resetAnalysisContext();
    Q_INVOKABLE void resetAllAnalysisFilters();
    Q_INVOKABLE void setPanelActive(const QString& key, bool active);
    Q_INVOKABLE void focusTimingId(const QString& idText = QString());
    Q_INVOKABLE void focusValueId(const QString& idText = QString());
    Q_INVOKABLE void focusAlarmId(const QString& idText = QString());
    Q_INVOKABLE bool seekReplayIssue(const QString& kind, int direction = 1);
    Q_INVOKABLE bool seekReplayId(const QString& idText, int direction = 1);
    Q_INVOKABLE bool jumpReplayToFrameIndex(int frameIndex, const QString& reasonText = QString());
    Q_INVOKABLE void toggleGraphSignal(const QString& key);
    Q_INVOKABLE void clearGraphSelection();
    Q_INVOKABLE void setGraphSelectedKeys(const QStringList& keys);
    Q_INVOKABLE void setGraphPresetKey(const QString& key);
    Q_INVOKABLE void setGraphDetailZoom(bool enabled);
    Q_INVOKABLE void setGraphWindowMs(int ms);
    Q_INVOKABLE QVariantList graphOverviewDetailSeries(double startMs, double endMs) const;
    Q_INVOKABLE bool seekReplayPrimaryIssue(int direction = 1);
    Q_INVOKABLE void setControlArmed(bool armed);
    Q_INVOKABLE void setControlTargetBus(int bus);
    Q_INVOKABLE void setControlTargetRpm(int rpm);
    Q_INVOKABLE void setControlTargetSteeringDeg(double deg);
    Q_INVOKABLE void controlKeyboardPress(const QString& key);
    Q_INVOKABLE void controlKeyboardRelease(const QString& key);
    Q_INVOKABLE void controlKeyboardReleaseAll();
    Q_INVOKABLE void controlKeyboardCommand(const QString& key);
    Q_INVOKABLE void controlSendManual();
    Q_INVOKABLE void controlSendNeutral();
    Q_INVOKABLE void controlEmergencyStop();
    Q_INVOKABLE void controlRunPattern(const QString& patternKey);
    Q_INVOKABLE void controlStopPattern();

signals:
    void availablePortsChanged();
    void connectedChanged();
    void transportModeChanged();
    void typedEvidenceChanged();
    void transportDiagnosticsChanged();
    void controlStateChanged();
    void statusTextChanged();
    void rulesPathChanged();
    void modelPathChanged();
    void logPathChanged();
    void replayPathChanged();
    void replayStateChanged();
    void liveStatsChanged();
    void liveUiPausedChanged();
    void rulesChanged();
    void signalDbChanged();
    void selectedValueIdChanged();
    void timingRowsChanged();
    void valueRowsChanged();
    void alarmRowsChanged();
    void sortOptionsChanged();
    void filtersChanged();
    void viewHoldChanged();
    void derivedSummaryChanged();
    void replayIssueMarkersChanged();
    void logStateChanged();
    void logTargetChanged();
    void graphCatalogChanged();
    void graphSelectionChanged();
    void graphSeriesChanged();
    void graphOverviewChanged();
    void operatorRecentEventsChanged();

private:
    using RuleSpec = CanModel::RuleSpec;
    using SignalSpec = CanModel::SignalSpec;
    using SignalMessageSpec = CanModel::SignalMessageSpec;

    struct IdState {
        bool seen = false;
        FrameRecord lastFrame;
        QString lastSource;
        qint64 lastLocalSeenMs = -1;
        quint64 lastBoardSeenUs = 0;
        double lastGapMs = -1.0;
        QString lastSeverity;
        QString lastReason;
        QString lastAlarmSeverity;
        QString activeAlarmKey;
        qint64 lastAlarmSeenMs = -1;
        QString activeValueAlarmKey;
        QString lastValueAlarmMessage;
        qint64 lastValueAlarmSeenMs = -1;
        int valueAlarmEventCount = 0;
        QString activeTimingAlarmKey;
        qint64 lastTimingAlarmSeenMs = -1;
        int lastTimingAgeBucket = -999;
        qint64 nextTimingEvalMs = std::numeric_limits<qint64>::max();
        QVariantMap cachedTimingRow;
        QVariantMap cachedPreviewInfo;
        quint64 cachedPreviewFingerprint = 0;
        QVariantMap cachedValueAlarmInfo;
        quint64 cachedValueAlarmFingerprint = 0;
        QVariantMap cachedValueRow;
        bool timingDerivedDirty = true;
        bool valueDerivedDirty = true;
        quint64 lastValueFingerprint = 0;
        QString lastValueRenderedSeverity;
        QString lastValueRenderedReason;
        QStringList timingEvents;
        int timingEventCount = 0;
        QString lastTimingIssueKey;
        bool timingIssueLatched = false;
    };

    struct ReplayIssueMarker {
        int index = -1;
        quint64 frameUs = 0;
        quint32 id = 0;
        QString kind;
        QString severity;
        QString note;
    };

public:
    struct GraphSignalDescriptor {
        QString key;
        quint32 id = 0;
        int signalIndex = -1;
        QString name;
        QString label;
        QString unit;
        QString group;
        QString historyKey;
        QString mode = QStringLiteral("raw");
        int colorIndex = 0;
    };

    struct GraphPoint {
        quint64 frameUs = 0;
        double value = 0.0;
    };

    struct GraphPresetSpec {
        QString key;
        QString title;
        QStringList seriesKeys;
    };

private:
    struct RecentOperatorEvent {
        qint64 wallMs = 0;
        QString category;
        QString level;
        QString summary;
        QString detail;
    };

    struct ReplayCheckpoint {
        int index = -1;
        quint64 displayedUs = 0;
        qint64 alarmSequence = 0;
        int timingMarkerCount = 0;
        int valueMarkerCount = 0;
        int alarmMarkerCount = 0;
        QHash<quint32, IdState> states;
        QVector<CanMonitorAnalysis::AlarmGroup> alarmGroups;
    };

    struct ControlPatternStep {
        int rpm = 0;
        double steeringDeg = 0.0;
        quint8 motorMode = 0;
        quint8 drivingMode = 1;
        int holdMs = 400;
        QString label;
    };

    using EvalResult = CanMonitorAnalysis::TimingEvalResult;

    void setStatus(const QString& text);
    void setReplayLoaded(bool loaded);
    void setReplayPlaying(bool playing);
    void updateReplayCursor(int index, int frameCount, quint64 currentUs, quint64 durationUs, double progress);
    int replayIndexForProgress(double progress) const;
    void clearReplaySeekState();
    void maybeStoreReplayCheckpoint(int index);
    static QString fmtReplayUs(quint64 us);
    void ensureTimeAnchorForFrame(const QString& source, quint64 frameUs);
    void loadReplayTimeMeta(const QString& replayBinPath);
    qint64 analysisNowMsForSource(const QString& source) const;
    QString timeTextForSourceUs(const QString& source, quint64 frameUs) const;
    QString timeTextForSourceMs(const QString& source, qint64 frameMs) const;
    quint64 replayAnalysisUs() const;
    bool loadModelFile(const QString& path);
    bool loadRulesFile(const QString& path);
    bool loadSignalDbFile(const QString& path);
    void ingestFrame(const FrameRecord& fr, const QString& source);
    EvalResult evaluateId(quint32 id, const IdState* state, qint64 nowMs) const;
    void refreshDerivedModels();
    void refreshTimingRows();
    void refreshValueRows();
    void refreshValueDetails();
    void maybeRefreshValueDetails(bool immediate = false);
    void invalidateValueDetailSignalCache();
    QString displayNameForId(quint32 id) const;
    QVector<QVariantMap> stabilizeRows(const QVector<QVariantMap>& sortedRows, const StableMapListModel& model, bool forceReorder) const;
    void updateTimingHistory(IdState& state, quint32 id, const EvalResult& eval, const QString& source, qint64 nowMs);
    void refreshAlarmRows();
    void clearDerivedRows();
    void restoreSessionState();
    void saveSessionState() const;
    int countIssueRows(const StableMapListModel& model) const;
    QString makeTopSummary(const StableMapListModel& model, const QString& textField) const;
    void refreshDerivedSummaryCache();
    void updateOperatorRecentEventsLocked();
    void pushOperatorRecentEvent(const QString& category, const QString& level, const QString& summary, const QString& detail = QString());
    void requestDerivedSummaryRefresh(bool immediate = false);
    void flushDerivedSummaryRefresh();
    void requestLogStateRefresh(bool immediate = false);
    void flushLogStateRefresh();
    void requestLiveStatsRefresh(bool immediate = false);
    void flushLiveStatsRefresh();
    void updateTransportDiagnostics();
    void rebuildGraphCatalog();
    void clearGraphHistory(const QString& source = QString());
    void appendGraphSamples(const FrameRecord& fr, const QString& source);
    void rebuildReplayGraphHistoryWindow();
    void invalidateGraphBucketCache(const QString& source, const QString& historyKey = QString());
    void rebuildGraphBucketCacheForSeries(const QString& source, const QString& historyKey, quint64 bucketUs);
    void rebuildGraphBucketCachesForSource(const QString& source);
    void appendGraphBucketCaches(const QString& source, const QString& historyKey, const GraphPoint& point);
    void trimGraphBucketCaches(const QString& source, const QString& historyKey, quint64 minUs);
    QVector<GraphBucketPoint> sliceGraphBucketCache(const QString& source, const QString& historyKey, quint64 startUs, quint64 endUs, quint64 bucketUs, int reserveCount);
    void clearGraphOverviewState();
    QStringList normalizedGraphOverviewKeys(const QStringList& keys) const;
    bool graphOverviewCacheCoversSelection(const QStringList& selected, quint64 startUs, quint64 endUs) const;
    void reuseGraphOverviewCacheForSelection(const QStringList& selected, quint64 startUs, quint64 endUs);
    void restartGraphOverviewBuild(bool clearSeries = true);
    void processGraphOverviewBuildStep();
    void refreshGraphOverviewSeries();
    QVariantList buildGraphOverviewDetailSeries(double startMs, double endMs) const;
    void resetGraphDetailZoomLock();
    void requestGraphRefresh(bool immediate = false);
    void flushGraphRefresh();
    void processTimingAnalysisSlice();
    void rebuildTimingEvalIdCache(const QString& source);
    QVector<quint32>& timingEvalIdsForSource(const QString& source);
    int& timingEvalCursorForSource(const QString& source);
    qint64& timingEvalCacheWallMsForSource(const QString& source);
    bool timingScopeActive() const;
    bool valueScopeActive() const;
    bool alarmScopeActive() const;
    int timingProjectionIntervalMs() const;
    int valueProjectionIntervalMs() const;
    int valueDetailProjectionIntervalMs() const;
    int alarmProjectionIntervalMs() const;
    bool projectionDue(qint64 lastMs, int minIntervalMs) const;
    bool projectionBackpressureActive() const;
    bool timingStructureSyncAllowed() const;
    bool valueStructureSyncAllowed() const;
    bool alarmStructureSyncAllowed() const;
    static QString normalizeOverviewBody(const QString& text);
    bool replayAnalysisActive() const;
    bool analysisPaused() const;
    void setReplayAnalysisHeld(bool held);
    QHash<quint32, IdState>& stateMapForSource(const QString& source);
    const QHash<quint32, IdState>& activeStateMap() const;
    QHash<quint32, IdState>& activeStateMap();
    QVector<CanMonitorAnalysis::AlarmGroup>& alarmGroupsForSource(const QString& source);
    const QVector<CanMonitorAnalysis::AlarmGroup>& activeAlarmGroups() const;
    QVector<CanMonitorAnalysis::AlarmGroup>& activeAlarmGroups();
    void handleAnalysisSourceMaybeChanged(bool previousReplayActive);
    void markAllAnalysisDirty(bool reorder = false);
    void syncLiveBusHealthAlarms();
    void flushPendingLiveFrames();
    void queueLiveViewBatch(const FrameRecordList& frames, const QStringList& timeTexts);
    void resetTypedEvidenceState();
    bool controlEvidenceReady() const;
    QString controlEvidenceBlockReason() const;
    bool controlTargetBusAllowed() const;
    bool controlPolicyAllowsTargetBus() const;
    int controlPolicyMaxRpm() const;
    double controlPolicyMaxAbsSteeringDeg() const;
    QString controlPolicyTargetRole() const;
    QSet<quint32> controlPolicyFingerprints() const;
    void updateControlBusCapability(const TypedCapabilityRecord& capability);
    void seedBusRoleResolver();
    void observeBusRoleFingerprint(quint8 bus, quint32 canId);
    bool autoSelectSystemControlBus();
    QString controlBusResolutionSummary() const;
    void queueControlHostFrame(const QByteArray& frame, const QString& summary, const QString& stage, quint32 commandId = 0, quint32 canId = 0, quint8 bus = 0);
    void sendControlHeartbeat(const QString& reason);
    void sendControlSession(quint8 action, const QString& reason);
    void maintainControlLeaseAndHeartbeat(bool forceHeartbeat = false, bool forceLeaseRenew = false);
    void sendControlBurst(int signedCommand, int rpm, double steeringDeg, quint8 motorMode, quint8 drivingMode, const QString& reason);
    void setControlTargetCommand(int signedCommand, int rpm, double steeringDeg, quint8 motorMode, quint8 drivingMode, const QString& reason);
    void applyControlKeyboardHeldState(const QString& reason, bool forceBurst);
    void updateControlWorkerCycleTarget();
    void sendControlKeepaliveTick(bool force = false, bool resetSlew = false);
    void sendControlSafetyStop(const QString& reason, bool disarmAfter);
    bool prepareControlSafeStopForDisconnect(const QString& reason);
    void applyControlSteeringPreset(double steeringDeg, const QString& reason);
    void startControlKeepalive();
    void stopControlKeepalive();
    void appendControlEvidenceEvent(const QString& stage, const QString& level, const QString& summary, const QString& detail = QString(), quint32 commandId = 0, quint32 canId = 0, quint8 bus = 0);
    void refreshControlStatus(const QString& status);
    void processControlPatternStep();
    void flushQueuedLiveViewBatch();
    void noteBusAlarmEvent(const QString& source);
    qint64 pendingLiveFrameCount() const;
    void appendPendingLiveFrames(const FrameRecordList& frames);
    int liveFlushChunkForBacklog(qint64 backlog) const;
    int liveFlushBudgetForBacklog(qint64 backlog) const;
    void compactPendingLiveFrames();
    void updateNextTimingEvalMs(quint32 id, IdState& state, qint64 nowMs);
    bool analyzeTimingState(quint32 id, IdState& state, const QString& source, qint64 nowMs);
    bool syncValueAlarmState(quint32 id, IdState& state, const QString& source, bool allowReplayMarkers);
    void advanceReplayHistoryToUs(quint64 frameUs);
    void syncReplayValueAlarm(quint32 id, IdState& state);
    void clearReplayIssueMarkers();
    void clearReplayTypedDiagnostics();
    void setReplayTypedDiagnosticsFromReader(const TypedReplayReader& reader, int canRxFrameCount);
    void appendReplayIssueMarker(const QString& kind, quint32 id, const QString& severity, const QString& note);
    const QVector<ReplayIssueMarker>& replayIssueMarkersForKind(const QString& kind) const;
    void captureReplaySnapshotState();
    void clearReplaySnapshotState();
    void restoreReplaySnapshotState();
    const QHash<quint32, IdState>& replaySnapshotStateMap() const;
    const QVector<CanMonitorAnalysis::AlarmGroup>& replaySnapshotAlarmGroups() const;
    const QVector<ReplayIssueMarker>& replaySnapshotMarkersForKind(const QString& kind) const;
    quint64 replaySnapshotDisplayedUs() const;
    int replaySnapshotObservedIdCount() const;
    void cancelReplayRebuild(bool restoreSnapshot = false);
    void processReplayRebuildStep();
    int replayCheckpointStride() const;
    bool rebuildReplayToIndex(int index, const QString& reasonText = QString());
    bool jumpReplayToIndex(int index, const QString& reasonText = QString());
    bool shouldTrackTimingForId(quint32 id) const;
    bool hasAlarmCapableSignals(quint32 id) const;
    int cumulativeTimingCountFor(const QHash<quint32, IdState>& states) const;
    int cumulativeValueAlarmCountFor(const QHash<quint32, IdState>& states) const;

    struct AnalysisViewState {
        QString timingFilterId;
        QString timingFilterSeverity;
        QString timingFilterName;
        QString timingFilterReason;
        QString timingFilterExpected;
        QString timingFilterGap;
        QString timingFilterAge;
        QString timingFilterSource;
        QString valueFilterId;
        QString valueFilterSeverity;
        QString valueFilterName;
        QString valueFilterSource;
        QString valueFilterRaw;
        QString valueFilterGap;
        QString valueFilterReason;
        QString alarmFilterId;
        QString alarmFilterSeverity;
        QString alarmFilterTime;
        QString alarmFilterName;
        QString alarmFilterSource;
        QString alarmFilterMessage;
        QString alarmFilterText;
        bool hasSelectedValueId = false;
        quint32 selectedValueCanId = 0;
    };

    static bool moveOrReplaceFile(const QString& src, const QString& dst);
    static void removeIfExists(const QString& path);
    QString defaultTempLogDirectory() const;
    QString activeAnalysisSourceKey() const;
    QString viewStateSummaryFor(const AnalysisViewState& state) const;
    static QJsonObject viewStateToJson(const AnalysisViewState& state);
    AnalysisViewState& viewStateForSource(const QString& source);
    const AnalysisViewState& viewStateForSource(const QString& source) const;
    bool applyViewState(const AnalysisViewState& state);
    void syncActiveViewSelection();
    void resetViewState(AnalysisViewState& state);
    void applyActiveViewStateFromSource();
    void emitFilterStateChanged();

    struct SummaryCache {
        CanMonitorAnalysis::LevelSummary level;
        QString topId;
        int warnCount = 0;
        int errCount = 0;
    };

    SummaryCache m_timingSummaryCache;
    SummaryCache m_valueSummaryCache;
    SummaryCache m_alarmSummaryCache;
    CanMonitorAnalysis::LevelSummary m_systemSummaryCache;

    QStringList m_availablePorts;
    bool m_connected = false;
    bool m_replayLoaded = false;
    bool m_replayPlaying = false;
    bool m_replayAnalysisHeld = false;
    bool m_liveUiPaused = false;
    QString m_statusText = QStringLiteral("준비");
    QString m_rulesPath;
    bool m_modelEnabled = true;
    CanModel::PackMeta m_modelMeta;
    QString m_rulesActiveSource;
    QString m_rulesActivePath;
    bool m_rulesUsingBundled = false;
    QString m_logPath;
    bool m_logRecordingActive = false;
    bool m_logPendingSave = false;
    bool m_logStopping = false;
    bool m_logSaving = false;
    bool m_logTypedSession = false;
    QString m_logTempPath;
    QString m_logTempMetaPath;
    QString m_logTempModelPath;
    QString m_logSuggestedSavePath;
    QString m_logTargetDirectory;
    QString m_logTargetName;
    quint64 m_logRecordedBytes = 0;
    quint64 m_logRecordedFrameCount = 0;
    QString m_logLastSavedPath;
    QString m_replayPath;
    ReplayRuntime m_replayRuntime;
    int m_replayFrameCount = 0;
    int m_replayCurrentIndex = 0;
    double m_replayProgress = 0.0;
    quint64 m_replayCurrentUs = 0;
    quint64 m_replayDisplayedUs = 0;
    quint64 m_replayDurationUs = 0;
    quint64 m_replayPlayAnchorUs = 0;
    QElapsedTimer m_replayPlayClock;
    int m_replayAnalyzedIndex = -1;
    int m_replayProcessingIndex = -1;
    quint64 m_replayProcessingUs = 0;
    QDateTime m_liveBaseDateTime;
    quint64 m_liveBaseFrameUs = 0;
    quint64 m_liveLatestUs = 0;
    QDateTime m_replayBaseDateTime;
    quint64 m_replayBaseFrameUs = 0;
    double m_replaySpeed = 1.0;
    bool m_replayLoop = false;
    QString m_replayTypedCaptureState;
    QString m_replayTypedDiagnosticsSummary;
    QVariantList m_replayTypedDiagnostics;
    QTimer m_replaySeekTimer;
    int m_pendingReplaySeekIndex = -1;
    QVector<ReplayCheckpoint> m_replayCheckpoints;
    QVector<ReplayIssueMarker> m_replayTimingIssueMarkers;
    QVector<ReplayIssueMarker> m_replayValueIssueMarkers;
    QVector<ReplayIssueMarker> m_replayAlarmIssueMarkers;
    bool m_replaySnapshotValid = false;
    QHash<quint32, IdState> m_replaySnapshotStates;
    QVector<CanMonitorAnalysis::AlarmGroup> m_replaySnapshotAlarmGroups;
    qint64 m_replaySnapshotAlarmSequence = 0;
    quint64 m_replaySnapshotDisplayedUs = 0;
    int m_replaySnapshotAnalyzedIndex = -1;
    QVector<ReplayIssueMarker> m_replaySnapshotTimingIssueMarkers;
    QVector<ReplayIssueMarker> m_replaySnapshotValueIssueMarkers;
    QVector<ReplayIssueMarker> m_replaySnapshotAlarmIssueMarkers;
    QTimer m_replayRebuildTimer;
    bool m_replayRebuildActive = false;
    int m_replayRebuildTargetIndex = -1;
    int m_replayRebuildNextIndex = 0;
    int m_replayRebuildViewStart = 0;
    QString m_replayRebuildReason;
    int m_replayRebuildChunk = 320;
    int m_replayRebuildMinChunk = 48;
    int m_replayRebuildBudgetMs = 7;
    QTimer m_liveFlushTimer;
    QTimer m_liveViewFlushTimer;
    QTimer m_boardHealthWatchdogTimer;
    QTimer m_controlKeepaliveTimer;
    QTimer m_controlPatternTimer;
    QTimer m_graphRefreshTimer;
    QTimer m_graphOverviewBuildTimer;
    QHash<QString, GraphSignalDescriptor> m_graphSignals;
    QMultiHash<quint32, QString> m_graphKeysById;
    QVector<GraphPresetSpec> m_graphPresetSpecs;
    StableMapListModel m_graphCatalogModel;
    QVariantList m_graphCatalogCache;
    QVariantList m_graphPresetCache;
    QStringList m_graphSelectedKeys;
    QString m_graphPresetKey = QStringLiteral("manual");
    QVariantList m_graphSeriesCache;
    QString m_graphSourceSummaryCache = QStringLiteral("선택 그래프 없음");
    QString m_graphRangeSummaryCache = QStringLiteral("-");
    QString m_graphDetailZoomSummaryCache = QStringLiteral("고정축");
    bool m_graphDetailZoom = false;
    int m_graphWindowMs = 15000;
    QString m_graphDisplayRangeKey;
    double m_graphDisplayYMin = 0.0;
    double m_graphDisplayYMax = 1.0;
    bool m_graphDisplayRangeValid = false;
    QString m_graphDetailZoomLockKey;
    double m_graphDetailZoomLockYMin = 0.0;
    double m_graphDetailZoomLockYMax = 1.0;
    bool m_graphDetailZoomLockValid = false;
    QHash<QString, QVector<GraphPoint>> m_liveGraphHistory;
    QHash<QString, QVector<GraphPoint>> m_replayGraphHistory;
    QHash<QString, QVector<GraphPoint>> m_replayOverviewGraphHistory;
    QHash<QString, QHash<quint64, QVector<GraphBucketCachePoint>>> m_liveGraphBucketCache;
    QHash<QString, QHash<quint64, QVector<GraphBucketCachePoint>>> m_replayGraphBucketCache;
    quint64 m_replayGraphBuiltStartUs = 0;
    quint64 m_replayGraphBuiltEndUs = 0;
    QString m_replayGraphBuiltSelectionKey;
    bool m_replayGraphWindowValid = false;
    qint64 m_lastGraphRefreshWallMs = -1;
    QVariantList m_graphOverviewSeriesCache;
    QString m_graphOverviewSourceSummaryCache = QStringLiteral("재생 파일을 열면 전체 그래프가 준비됩니다");
    QString m_graphOverviewRangeSummaryCache = QStringLiteral("-");
    QString m_graphOverviewBuildTextCache = QStringLiteral("재생 파일을 열면 전체 그래프가 준비됩니다");
    bool m_graphOverviewBuildActive = false;
    double m_graphOverviewBuildProgress = 0.0;
    int m_graphOverviewBuildNextIndex = 0;
    QString m_graphOverviewBuildSelectionKey;
    QStringList m_graphOverviewBuildSelectedKeys;
    QSet<quint32> m_graphOverviewBuildIds;
    quint64 m_graphOverviewBuiltStartUs = 0;
    quint64 m_graphOverviewBuiltEndUs = 0;
    QString m_graphOverviewBuiltSelectionKey;
    FrameRecordList m_pendingLiveFrames;
    int m_pendingLiveFrameOffset = 0;
    FrameRecordList m_pendingLiveViewFrames;
    QStringList m_pendingLiveViewTimeTexts;
    int m_liveFlushChunk = 128;
    int m_liveFlushMinChunk = 24;
    int m_liveViewChunk = 72;
    int m_liveFlushBudgetMs = 2;
    quint64 m_liveSampledViewDrops = 0;
    quint64 m_liveProjectionObservedFrames = 0;
    quint64 m_liveProjectionProjectedFrames = 0;
    quint64 m_liveProjectionWorkerSampledFrames = 0;
    quint64 m_liveProjectionWorkerDroppedFrames = 0;
    quint64 m_liveProjectionObservedControlEvidenceRecords = 0;
    quint64 m_liveProjectionProjectedControlEvidenceRecords = 0;
    quint64 m_liveProjectionSampledControlEvidenceRecords = 0;
    quint64 m_liveProjectionDroppedFrames = 0;
    quint64 m_liveProjectionFlushBudgetHits = 0;
    int m_liveProjectionMaxBacklog = 0;
    int m_liveProjectionLastFlushMs = 0;
    qint64 m_lastLiveRuntimeLogWallMs = 0;
    qint64 m_lastLiveFrameWallMs = -1;
    qint64 m_lastLiveStatsWallMs = -1;
    StatsRecord m_lastStats;
    quint32 m_lastDroppedTotalObserved = 0;
    quint32 m_lastFifoOverflowTotalObserved = 0;
    qint64 m_lastDropBumpMs = -1;
    qint64 m_lastFifoBumpMs = -1;
    bool m_dropAlarmActive = false;
    bool m_fifoAlarmActive = false;
    bool m_errPassiveAlarmActive = false;
    bool m_busOffAlarmActive = false;
    int m_liveBusAlarmEventCount = 0;
    int m_replayBusAlarmEventCount = 0;
    qint64 m_lastRoutineControlWriteNotifyWallMs = 0;
    qint64 m_lastHostTxQueueNotifyWallMs = 0;

    StableMapListModel m_timingModel;
    StableMapListModel m_valueModel;
    StableMapListModel m_alarmModel;
    CanMonitorControl::ControlAuditModel m_controlAudit;
    DetailListModel m_valueDetailModel;
    bool m_hasSelectedValueId = false;
    quint32 m_selectedValueCanId = 0;
    QString m_timingSortMode = QStringLiteral("id");
    bool m_timingSortDescending = false;
    QString m_valueSortMode = QStringLiteral("id");
    bool m_valueSortDescending = false;
    QString m_alarmSortMode = QStringLiteral("id");
    bool m_alarmSortDescending = false;
    QString m_timingFilterId;
    QString m_timingFilterSeverity;
    QString m_timingFilterName;
    QString m_timingFilterReason;
    QString m_timingFilterExpected;
    QString m_timingFilterGap;
    QString m_timingFilterAge;
    QString m_timingFilterSource;
    QString m_valueFilterId;
    QString m_valueFilterSeverity;
    QString m_valueFilterName;
    QString m_valueFilterSource;
    QString m_valueFilterRaw;
    QString m_valueFilterGap;
    QString m_valueFilterReason;
    QString m_alarmFilterId;
    QString m_alarmFilterSeverity;
    QString m_alarmFilterTime;
    QString m_alarmFilterName;
    QString m_alarmFilterSource;
    QString m_alarmFilterMessage;
    QString m_alarmFilterText;
    AnalysisViewState m_liveViewState;
    AnalysisViewState m_replayViewState;

    QHash<quint32, RuleSpec> m_rules;
    QHash<quint32, SignalMessageSpec> m_signalMessages;
    QSet<quint32> m_alarmCapableSignalIds;
    QHash<quint32, IdState> m_liveStates;
    QHash<quint32, IdState> m_replayStates;
    QElapsedTimer m_uptime;
    QTimer m_operatorPulseTimer;
    QTimer m_analysisTimer;
    QTimer m_derivedSummaryTimer;
    QTimer m_logStateTimer;
    QTimer m_liveStatsTimer;
    bool m_derivedSummaryDirty = false;
    bool m_logStateDirty = false;
    bool m_liveStatsDirty = false;
    QVector<quint32> m_liveTimingEvalIds;
    QVector<quint32> m_replayTimingEvalIds;
    int m_liveTimingEvalCursor = 0;
    int m_replayTimingEvalCursor = 0;
    qint64 m_lastLiveTimingEvalCacheWallMs = -1;
    qint64 m_lastReplayTimingEvalCacheWallMs = -1;
    QTimer m_valueRefreshTimer;
    QTimer m_alarmRefreshTimer;
    qint64 m_lastTimingProjectionWallMs = -1;
    qint64 m_lastValueProjectionWallMs = -1;
    qint64 m_lastValueDetailProjectionWallMs = -1;
    qint64 m_lastAlarmProjectionWallMs = -1;
    quint32 m_cachedValueDetailCanId = 0;
    QString m_cachedValueDetailSource;
    quint64 m_cachedValueDetailFingerprint = 0;
    bool m_cachedValueDetailModelEnabled = true;
    QVector<DetailRow> m_cachedValueDetailSignalRows;
    qint64 m_lastTimingStructureSyncWallMs = -1;
    qint64 m_lastValueStructureSyncWallMs = -1;
    qint64 m_lastAlarmStructureSyncWallMs = -1;
    bool m_timingRowsDirty = true;
    bool m_valueRowsDirty = true;
    bool m_alarmRowsDirty = true;
    bool m_valueDetailsDirty = true;
    QString m_lastValueDetailSignature;
    bool m_timingReorderRequested = true;
    bool m_valueReorderRequested = true;
    bool m_alarmReorderRequested = true;
    bool m_timingViewHeld = false;
    bool m_valueViewHeld = false;
    bool m_alarmViewHeld = false;
    bool m_overviewPanelActive = true;
    bool m_livePanelActive = true;
    bool m_timingPanelActive = true;
    bool m_valuePanelActive = true;
    bool m_graphPageActive = false;
    QVector<RecentOperatorEvent> m_recentOperatorEvents;
    QString m_recentSourceKey;
    QString m_recentAnalysisKey;
    QString m_recentBusKey;
    QString m_recentActionKey;
    QString m_recentLogKey;
    QString m_recentReplayKey;
    QString m_recentModelKey;
    bool m_alarmPanelActive = true;
    QVector<CanMonitorAnalysis::AlarmGroup> m_liveAlarmGroups;
    QVector<CanMonitorAnalysis::AlarmGroup> m_replayAlarmGroups;
    qint64 m_liveAlarmSequence = 0;
    qint64 m_replayAlarmSequence = 0;

    FrameListModel m_recentFrames;
    FrameListModel m_liveFrames;
    FrameListModel m_replayFrames;
    FrameFilterProxyModel m_liveFrameView;
    FrameFilterProxyModel m_replayFrameView;
    mutable SessionManager m_session;
    bool m_restoringSession = false;

    SerialWorker* m_worker = nullptr;
    CanMonitorTransport::TransportRuntime m_transportRuntime;
    CanMonitorTransport::TransportSession m_transportSession;
    QString m_transportModeKey = QStringLiteral("typed");
    quint64 m_typedRecordCount = 0;
    quint64 m_typedBytesDropped = 0;
    quint64 m_typedCrcFailures = 0;
    quint64 m_typedLengthFailures = 0;
    quint64 m_typedVersionWarnings = 0;
    quint64 m_typedSeqGaps = 0;
    quint64 m_typedLastMonoUs = 0;
    QString m_typedLastRecordType;
    QString m_typedLastCanRxSummary;
    QString m_typedLastCanTxSummary;
    qint64 m_lastTypedEvidenceNotifyWallMs = 0;
    quint64 m_lastTypedHealthMonoUs = 0;
    quint32 m_lastTypedHealthCanRxTotal = 0;
    quint32 m_lastTypedHealthSerialTxTotal = 0;
    bool m_typedRxHealthParityAnchored = false;
    quint32 m_typedRxHealthAnchorBoardTotal = 0;
    quint64 m_typedRxHealthAnchorStreamCount = 0;
    quint64 m_typedRxHealthBoardDelta = 0;
    quint64 m_typedRxHealthStreamDelta = 0;
    qint64 m_typedRxHealthMissing = 0;
    QMap<quint8, quint64> m_typedCanRxByBus;
    QMap<quint8, quint64> m_typedCanTxByBus;
    QMap<quint8, quint64> m_typedTypeCounts;
    CanMonitorEvidence::EvidenceRuntime m_evidenceRuntime;
    CanMonitorControl::ControlRuntime m_controlRuntime;
    CanModel::ControlPolicySpec m_controlPolicy;
    bool m_controlCapabilityHasBusDescriptors = false;
    QSet<int> m_controlTxAllowedBuses;
    QString m_controlBusSummary = QStringLiteral("waiting for CAPABILITY bus descriptors");
    CanMonitorEvidence::BusRoleResolver m_busRoleResolver;
    QMap<quint8, quint64> m_systemFingerprintHitsByBus;
    QSet<QString> m_controlKeyboardHeldKeys;
    bool m_controlKeyboardSessionActive = false;
    int m_controlPatternIndex = 0;
    QVector<ControlPatternStep> m_controlPatternSteps;
    ReplayEngine m_replay;
};
