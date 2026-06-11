#include "AlarmManager.h"

namespace {

QString severityColor(const QString& severity) {
    if (severity == QStringLiteral("ERR")) return QStringLiteral("#c0392b");
    if (severity == QStringLiteral("WARN")) return QStringLiteral("#d97706");
    if (severity == QStringLiteral("OK")) return QStringLiteral("#118a42");
    if (severity == QStringLiteral("복구")) return QStringLiteral("#2563eb");
    if (severity == QStringLiteral("해제")) return QStringLiteral("#94a3b8");
    if (severity == QStringLiteral("미수신")) return QStringLiteral("#6b7280");
    return QStringLiteral("#52606d");
}

int severityRank(const QString& severity) {
    if (severity == QStringLiteral("ERR")) return 4;
    if (severity == QStringLiteral("WARN")) return 3;
    if (severity == QStringLiteral("OK")) return 2;
    if (severity == QStringLiteral("미수신")) return 1;
    return 0;
}

} // namespace

namespace CanMonitorAnalysis {

void AlarmManager::resolveValueAlarm(quint32,
                                     const QString&,
                                     QString& activeValueAlarmKey,
                                     QVector<AlarmGroup>& groups,
                                     bool& rowsDirty) {
    if (activeValueAlarmKey.isEmpty()) return;
    for (qsizetype i = 0; i < groups.size(); ++i) {
        if (groups[i].key != activeValueAlarmKey) continue;
        groups.removeAt(i);
        rowsDirty = true;
        break;
    }
    activeValueAlarmKey.clear();
}

void AlarmManager::syncValueAlarm(quint32 id,
                                  const ValueAlarmResult& info,
                                  const QString& name,
                                  const QString& source,
                                  const QString& timeText,
                                  qint64 frameSeenMs,
                                  QString& activeValueAlarmKey,
                                  qint64& lastValueAlarmSeenMs,
                                  QVector<AlarmGroup>& groups,
                                  qint64& alarmSequence,
                                  bool& rowsDirty) {
    const QString key = info.alarmKey;
    const bool frameAdvanced = (lastValueAlarmSeenMs != frameSeenMs);
    const QString nowText = timeText.trimmed().isEmpty() ? QStringLiteral("-") : timeText.trimmed();

    if (!activeValueAlarmKey.isEmpty() && activeValueAlarmKey != key) {
        resolveValueAlarm(id, info.message, activeValueAlarmKey, groups, rowsDirty);
    }

    AlarmGroup* group = nullptr;
    for (auto& item : groups) {
        if (item.key == key) {
            group = &item;
            break;
        }
    }

    if (!group) {
        AlarmGroup fresh;
        fresh.sequence = ++alarmSequence;
        fresh.key = key;
        fresh.id = id;
        fresh.timeText = nowText;
        fresh.severity = info.severity;
        fresh.severityColor = severityColor(info.severity);
        fresh.name = name;
        fresh.source = source;
        fresh.message = info.message;
        fresh.severityRank = severityRank(info.severity);
        fresh.active = true;
        fresh.updateCount = 1;
        fresh.category = QStringLiteral("value");
        fresh.metricText = info.metricText;
        fresh.gaugePct = info.gaugePct;
        fresh.history << QStringLiteral("[%1] %2").arg(nowText, info.message);
        groups.push_back(fresh);
        while (groups.size() > 120) groups.removeFirst();
        rowsDirty = true;
    } else {
        const bool wasInactive = !group->active || group->severity == QStringLiteral("해제");
        group->active = true;
        group->category = QStringLiteral("value");
        group->severity = info.severity;
        group->severityColor = severityColor(info.severity);
        group->name = name;
        group->source = source;
        group->metricText = info.metricText;
        group->gaugePct = info.gaugePct;
        group->severityRank = severityRank(info.severity);
        if (frameAdvanced) {
            group->updateCount += 1;
            rowsDirty = true;
        }
        if (group->message != info.message || wasInactive) {
            group->message = info.message;
            group->history.prepend(QStringLiteral("[%1] %2").arg(nowText, info.message));
            rowsDirty = true;
        }
        while (group->history.size() > 32) group->history.removeLast();
    }

    activeValueAlarmKey = key;
    lastValueAlarmSeenMs = frameSeenMs;
}

} // namespace CanMonitorAnalysis
