#include "perf/PerformanceProbeRuntime.h"

#include <QJsonArray>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace CanMonitorPerf {
namespace {

struct Metric {
    quint64 calls = 0;
    quint64 totalUs = 0;
    qint64 maxUs = 0;
    qint64 lastUs = 0;
    qint64 lastBacklog = -1;
    quint64 overBudget = 0;
    QVector<qint64> recentUs;
    int recentCursor = 0;
};

std::atomic_bool g_enabled{false};
QMutex g_mutex;
QHash<QString, Metric> g_metrics;

qint64 percentile95(QVector<qint64> values) {
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    const int size = int(values.size());
    const int index = std::clamp(int(std::ceil(double(size) * 0.95)) - 1, 0, size - 1);
    return values.at(index);
}

QVariantMap metricToVariant(const QString& name, const Metric& metric) {
    QVariantMap row;
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("calls"), QString::number(metric.calls));
    row.insert(QStringLiteral("totalMs"), double(metric.totalUs) / 1000.0);
    row.insert(QStringLiteral("avgUs"), metric.calls > 0 ? double(metric.totalUs) / double(metric.calls) : 0.0);
    row.insert(QStringLiteral("maxUs"), metric.maxUs);
    row.insert(QStringLiteral("p95Us"), percentile95(metric.recentUs));
    row.insert(QStringLiteral("lastUs"), metric.lastUs);
    row.insert(QStringLiteral("lastBacklog"), metric.lastBacklog);
    row.insert(QStringLiteral("overBudget"), QString::number(metric.overBudget));
    return row;
}

} // namespace

void PerformanceProbeRuntime::setEnabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_release);
}

bool PerformanceProbeRuntime::enabled() {
    return g_enabled.load(std::memory_order_acquire);
}

void PerformanceProbeRuntime::reset() {
    QMutexLocker locker(&g_mutex);
    g_metrics.clear();
}

void PerformanceProbeRuntime::record(const char* name, qint64 elapsedUs, qint64 backlog, qint64 budgetUs) {
    if (!enabled() || name == nullptr || *name == '\0' || elapsedUs < 0) return;
    QMutexLocker locker(&g_mutex);
    Metric& metric = g_metrics[QString::fromLatin1(name)];
    metric.calls += 1;
    metric.totalUs += quint64(elapsedUs);
    metric.maxUs = std::max(metric.maxUs, elapsedUs);
    metric.lastUs = elapsedUs;
    metric.lastBacklog = backlog;
    if (budgetUs > 0 && elapsedUs > budgetUs) metric.overBudget += 1;
    if (metric.recentUs.size() < 256) {
        metric.recentUs.push_back(elapsedUs);
    } else {
        metric.recentUs[metric.recentCursor] = elapsedUs;
        metric.recentCursor = (metric.recentCursor + 1) % metric.recentUs.size();
    }
}

QVariantList PerformanceProbeRuntime::rows(int limit) {
    QMutexLocker locker(&g_mutex);
    QVector<QPair<QString, Metric>> sorted;
    sorted.reserve(g_metrics.size());
    for (auto it = g_metrics.cbegin(); it != g_metrics.cend(); ++it) {
        sorted.push_back(qMakePair(it.key(), it.value()));
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.second.totalUs != b.second.totalUs) return a.second.totalUs > b.second.totalUs;
        return a.first < b.first;
    });
    QVariantList out;
    const int count = std::min(std::max(0, limit), int(sorted.size()));
    out.reserve(count);
    for (int i = 0; i < count; ++i) out.push_back(metricToVariant(sorted.at(i).first, sorted.at(i).second));
    return out;
}

QJsonObject PerformanceProbeRuntime::snapshot(int limit) {
    const QVariantList rowList = rows(limit);
    QJsonArray rowArray;
    for (const QVariant& row : rowList) rowArray.append(QJsonObject::fromVariantMap(row.toMap()));

    quint64 totalCalls = 0;
    quint64 totalUs = 0;
    {
        QMutexLocker locker(&g_mutex);
        for (const Metric& metric : std::as_const(g_metrics)) {
            totalCalls += metric.calls;
            totalUs += metric.totalUs;
        }
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), enabled());
    obj.insert(QStringLiteral("metric_count"), rowList.size());
    obj.insert(QStringLiteral("total_calls"), QString::number(totalCalls));
    obj.insert(QStringLiteral("total_ms"), double(totalUs) / 1000.0);
    obj.insert(QStringLiteral("rows"), rowArray);
    return obj;
}

QString PerformanceProbeRuntime::summary() {
    QMutexLocker locker(&g_mutex);
    if (!enabled()) return QStringLiteral("성능 계측 꺼짐");
    quint64 calls = 0;
    quint64 totalUs = 0;
    QString topName;
    quint64 topTotalUs = 0;
    for (auto it = g_metrics.cbegin(); it != g_metrics.cend(); ++it) {
        calls += it.value().calls;
        totalUs += it.value().totalUs;
        if (it.value().totalUs > topTotalUs) {
            topTotalUs = it.value().totalUs;
            topName = it.key();
        }
    }
    if (calls == 0) return QStringLiteral("성능 계측 켜짐 · 샘플 대기");
    return QStringLiteral("성능 계측 켜짐 · %1 calls · total %2 ms · top %3")
        .arg(calls)
        .arg(QString::number(double(totalUs) / 1000.0, 'f', 1))
        .arg(topName.isEmpty() ? QStringLiteral("-") : topName);
}

} // namespace CanMonitorPerf
