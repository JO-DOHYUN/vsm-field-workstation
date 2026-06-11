#pragma once

#include "SessionManager.h"

#include <QString>

namespace AppUiState {

struct AnalysisViewStateSnapshot {
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
    QString selectedValueId;
};

struct Snapshot {
    bool modelEnabled = true;
    bool modelBundled = true;
    QString modelPath;

    QString timingSortMode = QStringLiteral("id");
    bool timingSortDescending = false;
    QString valueSortMode = QStringLiteral("id");
    bool valueSortDescending = false;
    QString alarmSortMode = QStringLiteral("id");
    bool alarmSortDescending = false;

    AnalysisViewStateSnapshot liveViewState;
    AnalysisViewStateSnapshot replayViewState;

    QString liveFrameIdFilter;
    QString replayFrameIdFilter;
    int liveFrameBusFilter = -1;
    int replayFrameBusFilter = -1;
    double replaySpeed = 1.0;
    bool replayLoop = false;
    bool liveUiPaused = false;
    QString logTargetDirectory;
    QString logTargetName;
};

} // namespace AppUiState

class UiStateStore {
public:
    static constexpr int kSchemaVersion = 1;

    AppUiState::Snapshot load(const SessionManager& session) const;
    void save(SessionManager& session, const AppUiState::Snapshot& snapshot) const;

private:
    AppUiState::Snapshot loadLegacy(const SessionManager& session) const;
};
