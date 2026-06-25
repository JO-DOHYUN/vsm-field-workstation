#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector>

namespace CanMonitorPerf {

class UiResponsivenessRuntime : public QObject {
    Q_OBJECT
public:
    struct Snapshot {
        quint64 ticks = 0;
        qint64 lastDelayMs = 0;
        qint64 maxDelayMs = 0;
        qint64 p50DelayMs = 0;
        qint64 p95DelayMs = 0;
        quint64 stall100Ms = 0;
        quint64 stall500Ms = 0;
        quint64 stall1000Ms = 0;
        qint64 recoveryMaxMs = 0;
    };

    explicit UiResponsivenessRuntime(QObject* parent = nullptr);

    void start(int intervalMs = 50);
    void stop();
    void reset();
    Snapshot snapshot() const;
    QVariantList rows() const;
    QJsonObject toJson() const;
    QString summary() const;

signals:
    void snapshotChanged();

private slots:
    void tick();

private:
    static qint64 percentile(QVector<qint64> values, double pct);

    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_expectedNextMs = 0;
    int m_intervalMs = 50;
    quint64 m_ticks = 0;
    qint64 m_lastDelayMs = 0;
    qint64 m_maxDelayMs = 0;
    quint64 m_stall100Ms = 0;
    quint64 m_stall500Ms = 0;
    quint64 m_stall1000Ms = 0;
    qint64 m_recoveryMaxMs = 0;
    QVector<qint64> m_recentDelays;
    int m_recentCursor = 0;
};

} // namespace CanMonitorPerf
