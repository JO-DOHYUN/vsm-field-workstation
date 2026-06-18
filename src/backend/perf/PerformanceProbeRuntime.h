#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QVariantList>

namespace CanMonitorPerf {

class PerformanceProbeRuntime {
public:
    static void setEnabled(bool enabled);
    static bool enabled();
    static void reset();
    static void record(const char* name, qint64 elapsedUs, qint64 backlog = -1, qint64 budgetUs = -1);
    static QVariantList rows(int limit = 64);
    static QJsonObject snapshot(int limit = 64);
    static QString summary();
};

class ScopedProbe {
public:
    explicit ScopedProbe(const char* name, qint64 backlog = -1, qint64 budgetUs = -1)
        : m_name(name), m_backlog(backlog), m_budgetUs(budgetUs), m_enabled(PerformanceProbeRuntime::enabled()) {
        if (m_enabled) m_timer.start();
    }

    ~ScopedProbe() {
        if (!m_enabled) return;
        PerformanceProbeRuntime::record(m_name, m_timer.nsecsElapsed() / 1000, m_backlog, m_budgetUs);
    }

    ScopedProbe(const ScopedProbe&) = delete;
    ScopedProbe& operator=(const ScopedProbe&) = delete;

private:
    const char* m_name = "";
    qint64 m_backlog = -1;
    qint64 m_budgetUs = -1;
    bool m_enabled = false;
    QElapsedTimer m_timer;
};

} // namespace CanMonitorPerf
