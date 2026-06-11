#include "TimingEvaluator.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace {

int severityRankFor(const QString& severity) {
    if (severity == QStringLiteral("ERR")) return 4;
    if (severity == QStringLiteral("WARN")) return 3;
    if (severity == QStringLiteral("OK")) return 2;
    if (severity == QStringLiteral("관찰")) return 1;
    if (severity == QStringLiteral("미수신")) return 1;
    return 0;
}

QString fmtMs(double ms) {
    if (ms < 0.0) return QStringLiteral("-");
    return QString::number(ms, 'f', 1) + QStringLiteral(" ms");
}

QString fmtPct(double pct) {
    if (pct < 0.0) return QStringLiteral("-");
    return QString::number(pct, 'f', 1) + QStringLiteral(" %");
}

QString normalizeAlarmMessageKey(QString text) {
    text = text.toLower().trimmed();
    text.replace(QRegularExpression(QStringLiteral("([+-]?\\d+(?:\\.\\d+)?)(?:\\s*(%|ms|v|a|c|soc|soh)?)")), QStringLiteral("#"));
    text.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return text.trimmed();
}

} // namespace

namespace CanMonitorAnalysis {

int TimingEvaluator::timingAgeBucket(const CanModel::RuleSpec* rule, double ageMs) {
    if (ageMs < 0.0) return -1;
    if (!rule || !rule->timingEnabled || rule->timingMode.compare(QStringLiteral("monitor"), Qt::CaseInsensitive) == 0) return 0;
    if (rule->ttlErrMs > 0.0 && ageMs >= rule->ttlErrMs) return 2;
    if (rule->ttlWarnMs > 0.0 && ageMs >= rule->ttlWarnMs) return 1;
    return 0;
}

TimingEvalResult TimingEvaluator::evaluate(const TimingInput& input) {
    TimingEvalResult out;
    const auto* rule = input.modelEnabled ? input.rule : nullptr;

    out.name = !input.displayName.trimmed().isEmpty() ? input.displayName.trimmed() : QStringLiteral("미등록");
    out.source = input.seen ? input.source : QStringLiteral("-");
    out.gapMs = input.seen ? input.gapMs : -1.0;

    if (!input.seen) {
        out.severity = rule ? QStringLiteral("미수신") : QStringLiteral("모델없음");
        out.reason = rule ? QStringLiteral("모델 기준은 있으나 아직 수신되지 않음") : QStringLiteral("모델도 프레임도 없음");
        out.severityRank = severityRankFor(out.severity);
        out.metricText = QStringLiteral("-");
        return out;
    }

    if (!rule || !rule->timingEnabled || rule->timingMode.compare(QStringLiteral("monitor"), Qt::CaseInsensitive) == 0) {
        out.severity = QStringLiteral("관찰");
        out.reason = QStringLiteral("모델 기준 없음 · 관찰 전용(실제 gap/age만 표시)");
        out.ageMs = double(input.nowMs - input.lastLocalSeenMs);
        out.severityRank = severityRankFor(out.severity);
        out.metricText = QStringLiteral("-");
        out.gaugePct = 0.0;
        return out;
    }

    out.ageMs = double(input.nowMs - input.lastLocalSeenMs);

    const bool ttlWarn = rule->ttlWarnMs > 0.0 && out.ageMs >= rule->ttlWarnMs;
    const bool ttlErr = rule->ttlErrMs > 0.0 && out.ageMs >= rule->ttlErrMs;

    double devPct = -1.0;
    bool perWarn = false;
    bool perErr = false;
    if (out.gapMs >= 0.0 && rule->expectedPeriodMs > 0.0) {
        devPct = std::abs(out.gapMs - rule->expectedPeriodMs) * 100.0 / rule->expectedPeriodMs;
        perWarn = rule->periodWarnPct > 0.0 && devPct >= rule->periodWarnPct;
        perErr = rule->periodErrPct > 0.0 && devPct >= rule->periodErrPct;
    }
    out.deviationPct = devPct;

    QStringList reasons;
    QStringList keyParts;
    if (ttlErr) {
        reasons << QStringLiteral("TTL %1 >= ERR %2").arg(fmtMs(out.ageMs), fmtMs(rule->ttlErrMs));
        keyParts << QStringLiteral("ttl_err");
    } else if (ttlWarn) {
        reasons << QStringLiteral("TTL %1 >= WARN %2").arg(fmtMs(out.ageMs), fmtMs(rule->ttlWarnMs));
        keyParts << QStringLiteral("ttl_warn");
    }

    if (perErr) {
        reasons << QStringLiteral("주기 편차 %1 >= ERR %2 (기대 %3 / 실제 %4)").arg(fmtPct(devPct), fmtPct(rule->periodErrPct), fmtMs(rule->expectedPeriodMs), fmtMs(out.gapMs));
        keyParts << QStringLiteral("period_err");
    } else if (perWarn) {
        reasons << QStringLiteral("주기 편차 %1 >= WARN %2 (기대 %3 / 실제 %4)").arg(fmtPct(devPct), fmtPct(rule->periodWarnPct), fmtMs(rule->expectedPeriodMs), fmtMs(out.gapMs));
        keyParts << QStringLiteral("period_warn");
    }

    if (ttlErr || perErr) out.severity = QStringLiteral("ERR");
    else if (ttlWarn || perWarn) out.severity = QStringLiteral("WARN");
    else out.severity = QStringLiteral("OK");

    if (reasons.isEmpty()) {
        if (out.gapMs >= 0.0 && rule->expectedPeriodMs > 0.0) {
            out.reason = QStringLiteral("기준 내 수신중 (기대 %1 / 실제 %2 / 경과 %3)")
                             .arg(fmtMs(rule->expectedPeriodMs), fmtMs(out.gapMs), fmtMs(out.ageMs));
        } else {
            out.reason = QStringLiteral("첫 수신 이후 기준 내 유지중 (경과 %1)").arg(fmtMs(out.ageMs));
        }
    } else {
        out.reason = reasons.join(QStringLiteral(" / "));
    }

    if (devPct >= 0.0) {
        out.metricText = fmtPct(devPct);
        const double denom = rule->periodErrPct > 0.0 ? rule->periodErrPct : (rule->periodWarnPct > 0.0 ? rule->periodWarnPct : 100.0);
        out.gaugePct = std::clamp((devPct / std::max(denom, 1.0)) * 100.0, 0.0, 100.0);
    } else if (out.ageMs >= 0.0 && (rule->ttlErrMs > 0.0 || rule->ttlWarnMs > 0.0)) {
        const double ttlBase = rule->ttlErrMs > 0.0 ? rule->ttlErrMs : rule->ttlWarnMs;
        out.metricText = QStringLiteral("TTL %1").arg(fmtMs(out.ageMs));
        out.gaugePct = std::clamp((out.ageMs / std::max(ttlBase, 1.0)) * 100.0, 0.0, 100.0);
    } else {
        out.metricText = QStringLiteral("-");
        out.gaugePct = 0.0;
    }

    out.activeAlarm = (out.severity == QStringLiteral("WARN") || out.severity == QStringLiteral("ERR"));
    out.alarmKey = keyParts.isEmpty() ? QString() : QStringLiteral("%1|%2").arg(input.id).arg(keyParts.join(QStringLiteral("+")));
    out.severityRank = severityRankFor(out.severity);
    return out;
}

} // namespace CanMonitorAnalysis
