#pragma once

#include "SerialWorker.h"

#include <QJsonObject>
#include <QThread>
#include <QString>

#include <functional>

namespace CanMonitorTransport {

class TransportRuntime {
public:
    ~TransportRuntime();

    SerialWorker* createWorker();
    void attachWorker(SerialWorker* worker);
    void startWorkerThread(QThread::Priority priority = QThread::InheritPriority);
    void shutdown();

    SerialWorker* worker() const { return m_worker; }
    bool hasWorker() const { return m_worker != nullptr; }
    bool ownsWorker() const { return m_ownsWorker; }
    bool workerThreadRunning() const { return m_workerThread.isRunning(); }
    bool liveProductionTypedOnly() const { return true; }
    SerialWorker::TransportMode requiredLiveMode() const { return SerialWorker::TransportMode::TypedEvidence; }

    static QString normalizeModeKey(const QString& raw);
    static SerialWorker::TransportMode modeForKey(const QString& key);

    bool enforceProductionLiveMode(QString* error = nullptr);
    bool setTransportModeKey(const QString& key, QString* error = nullptr);
    bool startSerial(const QString& portName, QString* error = nullptr);
    bool stopSerial(QString* error = nullptr);
    bool setTypedStorage(bool enable, const QString& sessionDir, const QJsonObject& metadata, QString* error = nullptr);
    bool setLegacyLogging(bool enable,
                          const QString& binPath,
                          const QString& metaPath,
                          const QString& rulesSnapshotPath,
                          const QString& rulesSourcePath,
                          QString* error = nullptr);
    bool sendHostFrame(const QByteArray& frame, const QString& summary, QString* error = nullptr);
    bool startControlCycle(int signedCommand,
                           int rpm,
                           double steeringDeg,
                           quint8 motorMode,
                           quint8 drivingMode,
                           quint8 bus,
                           int periodMs,
                           int frameGapMs,
                           QString* error = nullptr);
    bool updateControlCycle(int signedCommand,
                            int rpm,
                            double steeringDeg,
                            quint8 motorMode,
                            quint8 drivingMode,
                            quint8 bus,
                            QString* error = nullptr);
    bool stopControlCycle(QString* error = nullptr);
    bool sendControlCycleBurstOnce(int signedCommand,
                                   int rpm,
                                   double steeringDeg,
                                   quint8 motorMode,
                                   quint8 drivingMode,
                                   quint8 bus,
                                   const QString& reason,
                                   bool resetSlew = false,
                                   QString* error = nullptr);

private:
    bool ensureWorker(QString* error) const;
    bool queueWorkerCall(QString* error, const char* operation, std::function<void(SerialWorker*)> call);

    QThread m_workerThread;
    SerialWorker* m_worker = nullptr;
    bool m_ownsWorker = false;
};

} // namespace CanMonitorTransport
