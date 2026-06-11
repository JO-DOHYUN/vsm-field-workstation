#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace CanMonitorAnalysis {

struct TimingEvalResult {
    QString severity;
    QString reason;
    double ageMs = -1.0;
    double gapMs = -1.0;
    int severityRank = 0;
    QString name;
    QString source;
    double deviationPct = -1.0;
    QString metricText;
    double gaugePct = 0.0;
    QString alarmKey;
    bool activeAlarm = false;
};

struct SignalPreviewResult {
    QString plain;
    QString rich;
};

struct ValueAlarmResult {
    bool active = false;
    QString severity = QStringLiteral("OK");
    QString message;
    QString metricText = QStringLiteral("-");
    double gaugePct = 0.0;
    QString alarmKey;
    QString category = QStringLiteral("value");

    QVariantMap toVariantMap() const {
        QVariantMap out;
        out.insert(QStringLiteral("active"), active);
        out.insert(QStringLiteral("severity"), severity);
        out.insert(QStringLiteral("message"), message);
        out.insert(QStringLiteral("metricText"), metricText);
        out.insert(QStringLiteral("gaugePct"), gaugePct);
        out.insert(QStringLiteral("alarmKey"), alarmKey);
        out.insert(QStringLiteral("category"), category);
        return out;
    }
};

struct AlarmGroup {
    qint64 sequence = 0;
    QString key;
    quint32 id = 0;
    QString timeText;
    QString severity;
    QString severityColor;
    QString name;
    QString source;
    QString message;
    int severityRank = 0;
    bool active = false;
    int updateCount = 0;
    QString category;
    QString metricText;
    double gaugePct = 0.0;
    QStringList history;
};

struct LevelSummary {
    QString level = QStringLiteral("NONE");
    QString color = QStringLiteral("#94a3b8");
    int activeCount = 0;
    QString summary = QStringLiteral("표시 항목 없음");
};

} // namespace CanMonitorAnalysis
