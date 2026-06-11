#pragma once

#include "AnalysisTypes.h"
#include "ModelPack.h"

#include <QString>

namespace CanMonitorAnalysis {

struct TimingInput {
    quint32 id = 0;
    QString displayName;
    QString source;
    bool modelEnabled = false;
    bool seen = false;
    qint64 nowMs = 0;
    qint64 lastLocalSeenMs = -1;
    double gapMs = -1.0;
    const CanModel::RuleSpec* rule = nullptr;
};

class TimingEvaluator {
public:
    static TimingEvalResult evaluate(const TimingInput& input);
    static int timingAgeBucket(const CanModel::RuleSpec* rule, double ageMs);
};

} // namespace CanMonitorAnalysis
