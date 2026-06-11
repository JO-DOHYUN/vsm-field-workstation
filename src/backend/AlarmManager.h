#pragma once

#include "AnalysisTypes.h"

#include <QVector>

namespace CanMonitorAnalysis {

class AlarmManager {
public:
    static void syncValueAlarm(quint32 id,
                               const ValueAlarmResult& info,
                               const QString& name,
                               const QString& source,
                               const QString& timeText,
                               qint64 frameSeenMs,
                               QString& activeValueAlarmKey,
                               qint64& lastValueAlarmSeenMs,
                               QVector<AlarmGroup>& groups,
                               qint64& alarmSequence,
                               bool& rowsDirty);

    static void resolveValueAlarm(quint32 id,
                                  const QString& fallbackText,
                                  QString& activeValueAlarmKey,
                                  QVector<AlarmGroup>& groups,
                                  bool& rowsDirty);
};

} // namespace CanMonitorAnalysis
