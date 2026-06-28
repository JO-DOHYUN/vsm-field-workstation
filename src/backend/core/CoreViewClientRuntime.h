#pragma once

#include "core/CoreViewTypes.h"

#include <QJsonObject>
#include <QString>

#include <array>
#include <optional>

namespace CanMonitorCore {

class CoreViewClientRuntime {
public:
    struct Policy {
        bool enabled = false;
        int limit = 0;
    };

    struct ViewRequest {
        bool valid = false;
        QString viewName;
        quint64 sinceSeq = 0;
        int limit = 0;
        quint64 requestId = 0;
    };

    struct ApplyResult {
        bool accepted = false;
        bool stale = false;
        bool changed = false;
        QString viewName;
        QJsonObject snapshot;
        QJsonObject change;
        std::optional<ViewRequest> followup;
    };

    struct Status {
        quint64 changedNotifications = 0;
        quint64 invalidChanges = 0;
        quint64 queryRequests = 0;
        quint64 queryResponses = 0;
        quint64 skippedDisabled = 0;
        quint64 skippedInflight = 0;
        quint64 timedOutInflight = 0;
        quint64 failedInflight = 0;
        quint64 staleResponses = 0;
        int pendingViews = 0;
        int inflightViews = 0;
    };

    CoreViewClientRuntime();

    void reset();
    void setPolicy(CoreViewName viewName, int limit, bool enabled = true);
    std::optional<ViewRequest> noteViewChanged(const QJsonObject& change);
    std::optional<ViewRequest> noteRequestFailed(quint64 requestId);
    ApplyResult applySnapshot(quint64 requestId, bool changed, const QJsonObject& snapshot, const QJsonObject& change);

    Status status() const;
    QJsonObject statusJson() const;
    QJsonObject lastSnapshot(CoreViewName viewName) const;
    quint64 lastSeq(CoreViewName viewName) const;

private:
    struct State {
        Policy policy;
        quint64 lastSeq = 0;
        quint64 pendingSeq = 0;
        quint64 inflightRequestId = 0;
        qint64 inflightStartedMs = 0;
        bool pending = false;
        bool inflight = false;
        QJsonObject lastSnapshot;
    };

    static int indexOf(CoreViewName viewName);
    static qint64 currentTimeMs();
    static quint64 jsonSeq(const QJsonObject& object, const QString& key);
    static bool parseChange(const QJsonObject& change, CoreViewName* viewName, quint64* viewSeq);
    std::optional<ViewRequest> makeRequest(CoreViewName viewName);

    std::array<State, kCoreViewCount> m_states;
    Status m_status;
    quint64 m_nextRequestId = 1;
};

} // namespace CanMonitorCore
