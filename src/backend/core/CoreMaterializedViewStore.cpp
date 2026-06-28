#include "core/CoreMaterializedViewStore.h"

#include <QDateTime>
#include <QJsonValue>

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

quint64 jsonU64Value(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (value.isString()) return value.toString().toULongLong();
    if (value.isDouble()) return quint64(value.toDouble());
    return 0;
}

} // namespace

CoreMaterializedViewStore::CoreMaterializedViewStore() {
    m_maxItems.fill(kDefaultViewItemCap);
    m_primaryArrayKeys.fill(QString());
    setPolicy(CoreViewName::LiveLatest, kDefaultViewItemCap, QStringLiteral("frames"));
    setPolicy(CoreViewName::RawLedgerTail, kRawLedgerTailCap, QStringLiteral("frames"));
    setPolicy(CoreViewName::GraphBucket, kGraphBucketCap, QStringLiteral("points"));
}

void CoreMaterializedViewStore::clear() {
    for (auto& entry : m_entries) {
        entry = Entry{};
    }
    m_nextViewSeq = 1;
}

void CoreMaterializedViewStore::setPolicy(CoreViewName viewName, int maxItems, const QString& primaryArrayKey) {
    const int index = indexOf(viewName);
    m_maxItems[index] = std::max(1, maxItems);
    if (!primaryArrayKey.isEmpty()) {
        m_primaryArrayKeys[index] = primaryArrayKey;
    }
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
        result.snapshot.payload = payloadWithQueryLimit(query.viewName, result.snapshot.payload, query.limit);
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

QJsonObject CoreMaterializedViewStore::payloadWithQueryLimit(CoreViewName viewName, const QJsonObject& payload, int limit) const {
    if (limit <= 0) return payload;
    const QString arrayKey = m_primaryArrayKeys.at(indexOf(viewName));
    if (arrayKey.isEmpty()) return payload;

    QJsonObject output = payload;
    const QJsonValue value = output.value(arrayKey);
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.size() > limit) {
            const QJsonArray limited = tailArray(array, limit);
            output.insert(arrayKey, limited);
            output.insert(QStringLiteral("item_count"), limited.size());
            if (viewName == CoreViewName::RawLedgerTail) {
                const quint64 removed = quint64(array.size() - limited.size());
                output.insert(QStringLiteral("first_seq"),
                              QString::number(jsonU64Value(output, QStringLiteral("first_seq")) + removed));
            }
        }
    }
    return output;
}

} // namespace CanMonitorCore
