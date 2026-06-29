#include "core/CoreViewTypes.h"

namespace CanMonitorCore {

namespace {

struct NamePair {
    CoreViewName name;
    const char* text;
};

constexpr NamePair kViewNames[] = {
    {CoreViewName::CoreHealth, "core_health"},
    {CoreViewName::TransportSummary, "transport_summary"},
    {CoreViewName::CaptureProgress, "capture_progress"},
    {CoreViewName::AnalysisSnapshot, "analysis_snapshot"},
    {CoreViewName::LiveLatest, "live_latest"},
    {CoreViewName::RawLedgerTail, "raw_ledger_tail"},
    {CoreViewName::GraphBucket, "graph_bucket"},
    {CoreViewName::ControlAudit, "control_audit"},
    {CoreViewName::FatalDiagnostics, "fatal_diagnostics"},
    {CoreViewName::ProfileStatus, "profile_status"},
};

} // namespace

QString coreViewNameToString(CoreViewName name) {
    for (const auto& pair : kViewNames) {
        if (pair.name == name) {
            return QString::fromLatin1(pair.text);
        }
    }
    return QStringLiteral("unknown");
}

bool coreViewNameFromString(const QString& value, CoreViewName* out) {
    for (const auto& pair : kViewNames) {
        if (value == QLatin1String(pair.text)) {
            if (out) {
                *out = pair.name;
            }
            return true;
        }
    }
    return false;
}

QString coreViewSeverityToString(CoreViewSeverity severity) {
    switch (severity) {
    case CoreViewSeverity::Ok:
        return QStringLiteral("ok");
    case CoreViewSeverity::Warn:
        return QStringLiteral("warn");
    case CoreViewSeverity::Error:
        return QStringLiteral("error");
    case CoreViewSeverity::Fatal:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

bool coreViewSeverityFromString(const QString& value, CoreViewSeverity* out) {
    const auto assign = [out](CoreViewSeverity severity) {
        if (out) {
            *out = severity;
        }
        return true;
    };

    if (value == QLatin1String("ok")) {
        return assign(CoreViewSeverity::Ok);
    }
    if (value == QLatin1String("warn")) {
        return assign(CoreViewSeverity::Warn);
    }
    if (value == QLatin1String("error")) {
        return assign(CoreViewSeverity::Error);
    }
    if (value == QLatin1String("fatal")) {
        return assign(CoreViewSeverity::Fatal);
    }
    return false;
}

QJsonObject CaptureSeqRange::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("valid"), valid);
    if (valid) {
        object.insert(QStringLiteral("first"), QString::number(first));
        object.insert(QStringLiteral("last"), QString::number(last));
    }
    return object;
}

QJsonObject ViewChanged::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("view_name"), coreViewNameToString(viewName));
    object.insert(QStringLiteral("view_seq"), QString::number(viewSeq));
    object.insert(QStringLiteral("severity"), coreViewSeverityToString(severity));
    object.insert(QStringLiteral("timestamp_ms"), QString::number(timestampMs));
    object.insert(QStringLiteral("cheap_counts"), cheapCounts);
    return object;
}

QJsonObject ViewSnapshot::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("view_name"), coreViewNameToString(viewName));
    object.insert(QStringLiteral("view_seq"), QString::number(viewSeq));
    object.insert(QStringLiteral("severity"), coreViewSeverityToString(severity));
    object.insert(QStringLiteral("updated_at_ms"), QString::number(updatedAtMs));
    object.insert(QStringLiteral("dropped_display_count"), QString::number(droppedDisplayCount));
    object.insert(QStringLiteral("source_capture_seq_range"), sourceCaptureSeqRange.toJson());
    object.insert(QStringLiteral("payload"), payload);
    return object;
}

} // namespace CanMonitorCore
