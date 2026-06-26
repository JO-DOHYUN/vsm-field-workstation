#pragma once

#include "core/CoreViewTypes.h"

#include <QJsonArray>
#include <QVector>

#include <array>

namespace CanMonitorCore {

class CoreMaterializedViewStore {
public:
    CoreMaterializedViewStore();

    void clear();
    void setPolicy(CoreViewName viewName, int maxItems);

    ViewChanged updateView(CoreViewName viewName,
                           const QJsonObject& payload,
                           CoreViewSeverity severity = CoreViewSeverity::Ok,
                           const QJsonObject& cheapCounts = {},
                           const CaptureSeqRange& sourceRange = {},
                           quint64 droppedDisplayCount = 0,
                           qint64 nowMs = -1);

    ViewChanged updateArrayView(CoreViewName viewName,
                                const QString& arrayKey,
                                const QJsonArray& items,
                                CoreViewSeverity severity = CoreViewSeverity::Ok,
                                const QJsonObject& cheapCounts = {},
                                const CaptureSeqRange& sourceRange = {},
                                qint64 nowMs = -1,
                                quint64 droppedDisplayCountDelta = 0);

    ViewQueryResult queryView(const ViewQuery& query) const;
    quint64 viewSeq(CoreViewName viewName) const;
    QVector<ViewChanged> changes() const;

private:
    struct Entry {
        bool hasSnapshot = false;
        ViewChanged change;
        ViewSnapshot snapshot;
    };

    static int indexOf(CoreViewName viewName);
    static qint64 currentTimeMs();
    QJsonObject payloadWithQueryLimit(const QJsonObject& payload, int limit) const;

    std::array<Entry, kCoreViewCount> m_entries;
    std::array<int, kCoreViewCount> m_maxItems;
    quint64 m_nextViewSeq = 1;
};

} // namespace CanMonitorCore
