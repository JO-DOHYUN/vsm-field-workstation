#include "core/CoreMaterializedViewStore.h"

#include <QDateTime>

#include <algorithm>

namespace CanMonitorCore {

namespace {

constexpr int kDefaultViewItemCap = 256;
constexpr int kRawLedgerTailCap = 1024;
constexpr int kGraphBucketCap = 4096;

QJsonArray tailArray(const QJsonArray& input, int limit) {
    if (limit <= 0 || input.size() <= limit) {
        return input;
    }

    QJsonArray output;
    const int start = input.size() - limit;
    for (int i = start; i < input.size(); ++i) {
        output.append(input.at(i));
    }
    return output;
}

} // namespace

CoreMaterializedViewStore::CoreMaterializedViewStore() {
    m_maxItems.fill(kDefaultViewItemCap);
    setPolicy(CoreViewName::RawLedgerTail, kRawLedgerTailCap);
    setPolicy(CoreViewName::GraphBucket, kGraphBucketCap);
}

void CoreMaterializedViewStore::clear() {
    for (auto& entry : m_entries) {
        entry = Entry{};
    }
    m_nextViewSeq = 1;
}

void CoreMaterializedViewStore::setPolicy(CoreViewName viewName, int maxItems) {
    m_maxItems[indexOf(viewName)] = std::max(1, maxItems);
}

ViewChanged CoreMaterializedViewStore::updateView(CoreViewName viewName,
                                                  const QJsonObject& payload,
                                                  CoreViewSeverity severity,
                                                  const QJsonObject& cheapCounts,
                                                  const CaptureSeqRange& sourceRange,
                                                  quint64 droppedDisplayCount,
                                                  qint64 nowMs) {
    const int index = indexOf(viewName);
    Entry& entry = m_entries[index];
    const qint64 timestampMs = nowMs >= 0 ? nowMs : currentTimeMs();

    entry.change.viewName = viewName;
    entry.change.viewSeq = m_nextViewSeq++;
    entry.change.severity = severity;
    entry.change.timestampMs = timestampMs;
    entry.change.cheapCounts = cheapCounts;

    entry.snapshot.viewName = viewName;
    entry.snapshot.viewSeq = entry.change.viewSeq;
    entry.snapshot.severity = severity;
    entry.snapshot.updatedAtMs = timestampMs;
    entry.snapshot.droppedDisplayCount = droppedDisplayCount;
    entry.snapshot.sourceCaptureSeqRange = sourceRange;
    entry.snapshot.payload = payload;
    entry.hasSnapshot = true;

    return entry.change;
}

ViewChanged CoreMaterializedViewStore::updateArrayView(CoreViewName viewName,
                                                       const QString& arrayKey,
                                                       const QJsonArray& items,
                                                       CoreViewSeverity severity,
                                                       const QJsonObject& cheapCounts,
                                                       const CaptureSeqRange& sourceRange,
                                                       qint64 nowMs,
                                                       quint64 droppedDisplayCountDelta) {
    const int index = indexOf(viewName);
    const int cap = m_maxItems[index];
    const QJsonArray boundedItems = tailArray(items, cap);

    QJsonObject payload;
    payload.insert(arrayKey, boundedItems);
    payload.insert(QStringLiteral("item_count"), boundedItems.size());
    payload.insert(QStringLiteral("item_cap"), cap);

    quint64 dropped = m_entries[index].hasSnapshot ? m_entries[index].snapshot.droppedDisplayCount : 0;
    dropped += droppedDisplayCountDelta;
    if (items.size() > boundedItems.size()) {
        dropped += static_cast<quint64>(items.size() - boundedItems.size());
    }

    QJsonObject counts = cheapCounts;
    counts.insert(QStringLiteral("item_count"), boundedItems.size());
    counts.insert(QStringLiteral("item_cap"), cap);
    counts.insert(QStringLiteral("dropped_display_count"), QString::number(dropped));

    return updateView(viewName, payload, severity, counts, sourceRange, dropped, nowMs);
}

ViewQueryResult CoreMaterializedViewStore::queryView(const ViewQuery& query) const {
    ViewQueryResult result;
    const Entry& entry = m_entries[indexOf(query.viewName)];
    if (!entry.hasSnapshot || query.sinceSeq >= entry.snapshot.viewSeq) {
        return result;
    }

    result.changed = true;
    result.change = entry.change;
    result.snapshot = entry.snapshot;
    if (query.limit > 0) {
        result.snapshot.payload = payloadWithQueryLimit(result.snapshot.payload, query.limit);
    }
    return result;
}

quint64 CoreMaterializedViewStore::viewSeq(CoreViewName viewName) const {
    const Entry& entry = m_entries[indexOf(viewName)];
    return entry.hasSnapshot ? entry.snapshot.viewSeq : 0;
}

QVector<ViewChanged> CoreMaterializedViewStore::changes() const {
    QVector<ViewChanged> output;
    output.reserve(kCoreViewCount);
    for (const auto& entry : m_entries) {
        if (entry.hasSnapshot) {
            output.push_back(entry.change);
        }
    }
    return output;
}

int CoreMaterializedViewStore::indexOf(CoreViewName viewName) {
    const int index = static_cast<int>(viewName);
    if (index < 0 || index >= kCoreViewCount) {
        return 0;
    }
    return index;
}

qint64 CoreMaterializedViewStore::currentTimeMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

QJsonObject CoreMaterializedViewStore::payloadWithQueryLimit(const QJsonObject& payload, int limit) const {
    QJsonObject output = payload;
    for (auto it = output.begin(); it != output.end(); ++it) {
        if (it->isArray()) {
            const QJsonArray array = it->toArray();
            if (array.size() > limit) {
                it.value() = tailArray(array, limit);
            }
        }
    }
    return output;
}

} // namespace CanMonitorCore
