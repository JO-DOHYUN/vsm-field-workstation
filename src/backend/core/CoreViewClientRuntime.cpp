#include "core/CoreViewClientRuntime.h"

#include <QDateTime>
#include <QJsonValue>

#include <algorithm>

namespace CanMonitorCore {

namespace {
constexpr qint64 kViewInflightTimeoutMs = 1500;
}

CoreViewClientRuntime::CoreViewClientRuntime() {
    setPolicy(CoreViewName::LiveLatest, 32, true);
    setPolicy(CoreViewName::RawLedgerTail, 256, true);
    setPolicy(CoreViewName::TransportSummary, 1, true);
    setPolicy(CoreViewName::CaptureProgress, 1, true);
    setPolicy(CoreViewName::AnalysisSnapshot, 0, true);
    setPolicy(CoreViewName::FatalDiagnostics, 16, true);
}

void CoreViewClientRuntime::reset() {
    for (State& state : m_states) {
        const Policy policy = state.policy;
        state = State{};
        state.policy = policy;
    }
    m_status = Status{};
    m_nextRequestId = 1;
}

void CoreViewClientRuntime::setPolicy(CoreViewName viewName, int limit, bool enabled) {
    State& state = m_states.at(indexOf(viewName));
    state.policy.enabled = enabled;
    state.policy.limit = limit;
}

std::optional<CoreViewClientRuntime::ViewRequest> CoreViewClientRuntime::noteViewChanged(const QJsonObject& change) {
    CoreViewName viewName = CoreViewName::CoreHealth;
    quint64 viewSeq = 0;
    if (!parseChange(change, &viewName, &viewSeq)) {
        ++m_status.invalidChanges;
        return std::nullopt;
    }

    ++m_status.changedNotifications;
    State& state = m_states.at(indexOf(viewName));
    if (!state.policy.enabled) {
        ++m_status.skippedDisabled;
        return std::nullopt;
    }
    if (viewSeq <= state.lastSeq && !state.inflight) {
        return std::nullopt;
    }
    state.pending = true;
    state.pendingSeq = std::max(state.pendingSeq, viewSeq);
    if (state.inflight) {
        const qint64 nowMs = currentTimeMs();
        if (state.inflightStartedMs > 0 && nowMs - state.inflightStartedMs >= kViewInflightTimeoutMs) {
            state.inflight = false;
            state.inflightRequestId = 0;
            state.inflightStartedMs = 0;
            ++m_status.timedOutInflight;
        } else {
            ++m_status.skippedInflight;
            return std::nullopt;
        }
    }
    return makeRequest(viewName);
}

CoreViewClientRuntime::ApplyResult CoreViewClientRuntime::applySnapshot(quint64 requestId,
                                                                        bool changed,
                                                                        const QJsonObject& snapshot,
                                                                        const QJsonObject& change) {
    ApplyResult result;
    CoreViewName viewName = CoreViewName::CoreHealth;
    quint64 viewSeq = 0;
    if (!parseChange(change, &viewName, &viewSeq)) {
        if (!parseChange(snapshot, &viewName, &viewSeq)) {
            ++m_status.invalidChanges;
            return result;
        }
    }

    State& state = m_states.at(indexOf(viewName));
    if (!state.inflight || state.inflightRequestId != requestId) {
        ++m_status.staleResponses;
        result.stale = true;
        return result;
    }

    state.inflight = false;
    state.inflightRequestId = 0;
    state.inflightStartedMs = 0;
    ++m_status.queryResponses;
    result.accepted = true;
    result.changed = changed;
    result.viewName = coreViewNameToString(viewName);
    result.snapshot = snapshot;
    result.change = change;

    if (changed) {
        const quint64 snapshotSeq = jsonSeq(snapshot, QStringLiteral("view_seq"));
        state.lastSeq = std::max({state.lastSeq, viewSeq, snapshotSeq});
        state.lastSnapshot = snapshot;
    } else {
        state.lastSeq = std::max(state.lastSeq, viewSeq);
    }

    if (state.pendingSeq > state.lastSeq) {
        result.followup = makeRequest(viewName);
    } else {
        state.pending = false;
        state.pendingSeq = 0;
    }
    return result;
}

CoreViewClientRuntime::Status CoreViewClientRuntime::status() const {
    Status out = m_status;
    for (const State& state : m_states) {
        if (state.pending) ++out.pendingViews;
        if (state.inflight) ++out.inflightViews;
    }
    return out;
}

QJsonObject CoreViewClientRuntime::statusJson() const {
    const Status s = status();
    QJsonObject out;
    out.insert(QStringLiteral("core_view_changed_notifications"), QString::number(s.changedNotifications));
    out.insert(QStringLiteral("core_view_invalid_changes"), QString::number(s.invalidChanges));
    out.insert(QStringLiteral("core_view_query_requests"), QString::number(s.queryRequests));
    out.insert(QStringLiteral("core_view_query_responses"), QString::number(s.queryResponses));
    out.insert(QStringLiteral("core_view_skipped_disabled"), QString::number(s.skippedDisabled));
    out.insert(QStringLiteral("core_view_skipped_inflight"), QString::number(s.skippedInflight));
    out.insert(QStringLiteral("core_view_timed_out_inflight"), QString::number(s.timedOutInflight));
    out.insert(QStringLiteral("core_view_stale_responses"), QString::number(s.staleResponses));
    out.insert(QStringLiteral("core_view_pending_views"), s.pendingViews);
    out.insert(QStringLiteral("core_view_inflight_views"), s.inflightViews);
    return out;
}

QJsonObject CoreViewClientRuntime::lastSnapshot(CoreViewName viewName) const {
    return m_states.at(indexOf(viewName)).lastSnapshot;
}

quint64 CoreViewClientRuntime::lastSeq(CoreViewName viewName) const {
    return m_states.at(indexOf(viewName)).lastSeq;
}

int CoreViewClientRuntime::indexOf(CoreViewName viewName) {
    const int index = static_cast<int>(viewName);
    return index >= 0 && index < kCoreViewCount ? index : 0;
}

qint64 CoreViewClientRuntime::currentTimeMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

quint64 CoreViewClientRuntime::jsonSeq(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (value.isString()) return value.toString().toULongLong();
    if (value.isDouble()) return quint64(value.toDouble());
    return 0;
}

bool CoreViewClientRuntime::parseChange(const QJsonObject& change, CoreViewName* viewName, quint64* viewSeq) {
    CoreViewName parsedName = CoreViewName::CoreHealth;
    if (!coreViewNameFromString(change.value(QStringLiteral("view_name")).toString(), &parsedName)) {
        return false;
    }
    const quint64 parsedSeq = jsonSeq(change, QStringLiteral("view_seq"));
    if (parsedSeq == 0) {
        return false;
    }
    if (viewName) *viewName = parsedName;
    if (viewSeq) *viewSeq = parsedSeq;
    return true;
}

std::optional<CoreViewClientRuntime::ViewRequest> CoreViewClientRuntime::makeRequest(CoreViewName viewName) {
    State& state = m_states.at(indexOf(viewName));
    if (!state.policy.enabled || state.inflight) {
        return std::nullopt;
    }
    state.inflight = true;
    state.inflightRequestId = m_nextRequestId++;
    state.inflightStartedMs = currentTimeMs();
    ++m_status.queryRequests;

    ViewRequest request;
    request.valid = true;
    request.viewName = coreViewNameToString(viewName);
    request.sinceSeq = state.lastSeq;
    request.limit = state.policy.limit;
    request.requestId = state.inflightRequestId;
    return request;
}

} // namespace CanMonitorCore
