#pragma once

#include <QJsonObject>
#include <QString>

namespace CanMonitorCore {

enum class CoreViewName {
    CoreHealth = 0,
    TransportSummary,
    CaptureProgress,
    AnalysisSnapshot,
    LiveLatest,
    RawLedgerTail,
    GraphBucket,
    ControlAudit,
    FatalDiagnostics,
    ProfileStatus,
};

constexpr int kCoreViewCount = 10;

enum class CoreViewSeverity {
    Ok = 0,
    Warn,
    Error,
    Fatal,
};

QString coreViewNameToString(CoreViewName name);
bool coreViewNameFromString(const QString& value, CoreViewName* out);

QString coreViewSeverityToString(CoreViewSeverity severity);
bool coreViewSeverityFromString(const QString& value, CoreViewSeverity* out);

struct CaptureSeqRange {
    bool valid = false;
    quint64 first = 0;
    quint64 last = 0;

    QJsonObject toJson() const;
};

struct ViewChanged {
    CoreViewName viewName = CoreViewName::CoreHealth;
    quint64 viewSeq = 0;
    CoreViewSeverity severity = CoreViewSeverity::Ok;
    qint64 timestampMs = 0;
    QJsonObject cheapCounts;

    QJsonObject toJson() const;
};

struct ViewSnapshot {
    CoreViewName viewName = CoreViewName::CoreHealth;
    quint64 viewSeq = 0;
    CoreViewSeverity severity = CoreViewSeverity::Ok;
    qint64 updatedAtMs = 0;
    quint64 droppedDisplayCount = 0;
    CaptureSeqRange sourceCaptureSeqRange;
    QJsonObject payload;

    QJsonObject toJson() const;
};

struct ViewQuery {
    CoreViewName viewName = CoreViewName::CoreHealth;
    quint64 sinceSeq = 0;
    int limit = 0;
};

struct ViewQueryResult {
    bool changed = false;
    ViewChanged change;
    ViewSnapshot snapshot;
};

} // namespace CanMonitorCore
