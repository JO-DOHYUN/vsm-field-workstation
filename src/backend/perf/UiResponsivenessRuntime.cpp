#include "perf/UiResponsivenessRuntime.h"

#include <QJsonArray>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace CanMonitorPerf {

UiResponsivenessRuntime::UiResponsivenessRuntime(QObject* parent)
    : QObject(parent) {
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &UiResponsivenessRuntime::tick);
}

void UiResponsivenessRuntime::start(int intervalMs) {
    m_intervalMs = std::max(10, intervalMs);
    if (!m_clock.isValid()) m_clock.start();
    m_expectedNextMs = m_clock.elapsed() + m_intervalMs;
    m_timer.start(m_intervalMs);
}

void UiResponsivenessRuntime::stop() {
    m_timer.stop();
}

void UiResponsivenessRuntime::reset() {
    m_clock.restart();
    m_expectedNextMs = m_intervalMs;
    m_ticks = 0;
    m_lastDelayMs = 0;
    m_maxDelayMs = 0;
    m_stall100Ms = 0;
    m_stall500Ms = 0;
    m_stall1000Ms = 0;
    m_recoveryMaxMs = 0;
    m_recentDelays.clear();
    m_recentCursor = 0;
}

void UiResponsivenessRuntime::tick() {
    if (!m_clock.isValid()) {
        m_clock.start();
        m_expectedNextMs = m_intervalMs;
    }

    const qint64 nowMs = m_clock.elapsed();
    const qint64 delayMs = std::max<qint64>(0, nowMs - m_expectedNextMs);
    m_expectedNextMs = nowMs + m_intervalMs;
    m_ticks += 1;
    m_lastDelayMs = delayMs;
    m_maxDelayMs = std::max(m_maxDelayMs, delayMs);
    if (delayMs >= 100) m_stall100Ms += 1;
    if (delayMs >= 500) m_stall500Ms += 1;
    if (delayMs >= 1000) m_stall1000Ms += 1;
    if (delayMs >= 100) m_recoveryMaxMs = std::max(m_recoveryMaxMs, delayMs);

    if (m_recentDelays.size() < 512) {
        m_recentDelays.push_back(delayMs);
    } else {
        m_recentDelays[m_recentCursor] = delayMs;
        m_recentCursor = (m_recentCursor + 1) % m_recentDelays.size();
    }

    if (m_ticks % 10 == 0 || delayMs >= 100) emit snapshotChanged();
}

UiResponsivenessRuntime::Snapshot UiResponsivenessRuntime::snapshot() const {
    Snapshot s;
    s.ticks = m_ticks;
    s.lastDelayMs = m_lastDelayMs;
    s.maxDelayMs = m_maxDelayMs;
    s.p50DelayMs = percentile(m_recentDelays, 0.50);
    s.p95DelayMs = percentile(m_recentDelays, 0.95);
    s.stall100Ms = m_stall100Ms;
    s.stall500Ms = m_stall500Ms;
    s.stall1000Ms = m_stall1000Ms;
    s.recoveryMaxMs = m_recoveryMaxMs;
    return s;
}

QVariantList UiResponsivenessRuntime::rows() const {
    const Snapshot s = snapshot();
    QVariantList rows;
    auto add = [&rows](const QString& name, const QString& value, const QString& level, const QString& detail) {
        QVariantMap row;
        row.insert(QStringLiteral("name"), name);
        row.insert(QStringLiteral("value"), value);
        row.insert(QStringLiteral("level"), level);
        row.insert(QStringLiteral("detail"), detail);
        rows.push_back(row);
    };
    add(QStringLiteral("ui_event_loop"),
        QStringLiteral("p95 %1 ms / max %2 ms").arg(s.p95DelayMs).arg(s.maxDelayMs),
        s.stall1000Ms > 0 ? QStringLiteral("ERR") : (s.stall500Ms > 0 ? QStringLiteral("WARN") : QStringLiteral("OK")),
        QStringLiteral("ticks %1 last %2 ms p50 %3 ms stalls100 %4 stalls500 %5 stalls1000 %6 recovery_max %7 ms")
            .arg(s.ticks)
            .arg(s.lastDelayMs)
            .arg(s.p50DelayMs)
            .arg(s.stall100Ms)
            .arg(s.stall500Ms)
            .arg(s.stall1000Ms)
            .arg(s.recoveryMaxMs));
    return rows;
}

QJsonObject UiResponsivenessRuntime::toJson() const {
    const Snapshot s = snapshot();
    QJsonObject obj;
    obj.insert(QStringLiteral("ticks"), QString::number(s.ticks));
    obj.insert(QStringLiteral("last_delay_ms"), s.lastDelayMs);
    obj.insert(QStringLiteral("max_delay_ms"), s.maxDelayMs);
    obj.insert(QStringLiteral("p50_delay_ms"), s.p50DelayMs);
    obj.insert(QStringLiteral("p95_delay_ms"), s.p95DelayMs);
    obj.insert(QStringLiteral("stall_100ms_count"), QString::number(s.stall100Ms));
    obj.insert(QStringLiteral("stall_500ms_count"), QString::number(s.stall500Ms));
    obj.insert(QStringLiteral("stall_1000ms_count"), QString::number(s.stall1000Ms));
    obj.insert(QStringLiteral("recovery_max_ms"), s.recoveryMaxMs);
    return obj;
}

QString UiResponsivenessRuntime::summary() const {
    const Snapshot s = snapshot();
    return QStringLiteral("UI loop p95 %1 ms / max %2 ms / stalls>=500 %3")
        .arg(s.p95DelayMs)
        .arg(s.maxDelayMs)
        .arg(s.stall500Ms);
}

qint64 UiResponsivenessRuntime::percentile(QVector<qint64> values, double pct) {
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    const int size = int(values.size());
    const int index = std::clamp(int(std::ceil(double(size) * pct)) - 1, 0, size - 1);
    return values.at(index);
}

} // namespace CanMonitorPerf
