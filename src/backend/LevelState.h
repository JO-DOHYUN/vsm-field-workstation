#pragma once

#include "AnalysisTypes.h"
#include "StableMapListModel.h"

namespace CanMonitorAnalysis {

class LevelState {
public:
    static LevelSummary fromModel(const StableMapListModel& model, const QString& textField);
    static LevelSummary merge(const LevelSummary& timing, const LevelSummary& value, const LevelSummary& alarm);
    static QString colorForLevel(const QString& level);
    static int rankForLevel(const QString& level);
};

} // namespace CanMonitorAnalysis
