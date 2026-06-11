#include "LevelState.h"

namespace CanMonitorAnalysis {

int LevelState::rankForLevel(const QString& level) {
    if (level == QStringLiteral("ERR")) return 4;
    if (level == QStringLiteral("WARN")) return 3;
    if (level == QStringLiteral("OK")) return 2;
    if (level == QStringLiteral("NONE")) return 1;
    return 0;
}

QString LevelState::colorForLevel(const QString& level) {
    if (level == QStringLiteral("ERR")) return QStringLiteral("#c0392b");
    if (level == QStringLiteral("WARN")) return QStringLiteral("#d97706");
    if (level == QStringLiteral("OK")) return QStringLiteral("#118a42");
    return QStringLiteral("#94a3b8");
}

LevelSummary LevelState::fromModel(const StableMapListModel& model, const QString& textField) {
    LevelSummary out;
    bool hasWarn = false;
    bool hasErr = false;
    QStringList lines;
    const int rowCount = model.rowCount();
    for (int i = 0; i < rowCount; ++i) {
        const QVariantMap row = model.get(i);
        const QString severity = row.value(QStringLiteral("severity")).toString();
        const bool active = row.value(QStringLiteral("active")).toBool();
        const bool issue = active || severity == QStringLiteral("WARN") || severity == QStringLiteral("ERR");
        if (!issue) continue;
        out.activeCount += 1;
        hasWarn = hasWarn || severity == QStringLiteral("WARN");
        hasErr = hasErr || severity == QStringLiteral("ERR");
        if (lines.size() < 3) {
            const QString id = row.value(QStringLiteral("idText")).toString();
            const QString body = row.value(textField).toString().trimmed();
            const QString sevPart = severity.isEmpty() ? QString() : QStringLiteral("[%1] ").arg(severity);
            lines << QStringLiteral("%1%2 %3").arg(sevPart, id, body).trimmed();
        }
    }
    if (hasErr) out.level = QStringLiteral("ERR");
    else if (hasWarn) out.level = QStringLiteral("WARN");
    else if (rowCount > 0) out.level = QStringLiteral("OK");
    else out.level = QStringLiteral("NONE");
    out.color = colorForLevel(out.level);
    out.summary = lines.isEmpty() ? QStringLiteral("표시 항목 없음") : lines.join(QStringLiteral("\n"));
    return out;
}

LevelSummary LevelState::merge(const LevelSummary& timing, const LevelSummary& value, const LevelSummary& alarm) {
    LevelSummary out;
    out.activeCount = timing.activeCount + value.activeCount + alarm.activeCount;
    const LevelSummary arr[3] = {timing, value, alarm};
    int bestIndex = -1;
    int bestRank = -1;
    for (int i = 0; i < 3; ++i) {
        const int rank = rankForLevel(arr[i].level);
        if (rank > bestRank) {
            bestRank = rank;
            bestIndex = i;
        }
    }
    if (bestIndex >= 0) {
        out.level = arr[bestIndex].level;
        out.color = colorForLevel(out.level);
        out.summary = arr[bestIndex].summary;
    }
    return out;
}

} // namespace CanMonitorAnalysis
