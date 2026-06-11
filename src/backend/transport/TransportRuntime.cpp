#include "transport/TransportRuntime.h"

#include <QMetaObject>
#include <utility>

namespace CanMonitorTransport {

TransportRuntime::~TransportRuntime() {
    shutdown();
}

SerialWorker* TransportRuntime::createWorker() {
    if (m_worker && m_ownsWorker) return m_worker;

    shutdown();
    m_worker = new SerialWorker();
    m_ownsWorker = true;
    m_worker->moveToThread(&m_workerThread);
    QObject::connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    return m_worker;
}

void TransportRuntime::attachWorker(SerialWorker* worker) {
    if (m_worker == worker) return;
    shutdown();
    m_worker = worker;
    m_ownsWorker = false;
}

void TransportRuntime::startWorkerThread(QThread::Priority priority) {
    if (!m_worker || !m_ownsWorker || m_workerThread.isRunning()) return;
    m_workerThread.start(priority);
}

void TransportRuntime::shutdown() {
    if (m_worker && m_ownsWorker && m_workerThread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, &SerialWorker::stop, Qt::BlockingQueuedConnection);
        m_workerThread.quit();
        m_workerThread.wait();
    } else if (m_worker && m_ownsWorker) {
        delete m_worker;
    }

    m_worker = nullptr;
    m_ownsWorker = false;
}

QString TransportRuntime::normalizeModeKey(const QString& raw) {
    const QString key = raw.trimmed().toLower();
    if (key == QStringLiteral("typed") ||
        key == QStringLiteral("typed_evidence") ||
        key == QStringLiteral("typed-evidence")) {
        return QStringLiteral("typed");
    }
    return QStringLiteral("legacy20");
}

SerialWorker::TransportMode TransportRuntime::modeForKey(const QString& key) {
    return normalizeModeKey(key) == QStringLiteral("typed")
        ? SerialWorker::TransportMode::TypedEvidence
        : SerialWorker::TransportMode::Legacy20;
}

bool TransportRuntime::ensureWorker(QString* error) const {
    if (m_worker) {
        if (error) error->clear();
        return true;
    }
    if (error) *error = QStringLiteral("transport worker is not attached");
    return false;
}

bool TransportRuntime::queueWorkerCall(QString* error, const char* operation, std::function<void(SerialWorker*)> call) {
    if (!ensureWorker(error)) return false;
    SerialWorker* worker = m_worker;
    const bool queued = QMetaObject::invokeMethod(worker, [worker, call = std::move(call)]() mutable {
        if (worker) call(worker);
    }, Qt::QueuedConnection);
    if (!queued) {
        if (error) *error = QStringLiteral("failed to queue transport operation: %1").arg(QString::fromLatin1(operation));
        return false;
    }
    if (error) error->clear();
    return true;
}

bool TransportRuntime::enforceProductionLiveMode(QString* error) {
    return queueWorkerCall(error, "enforceProductionLiveMode", [](SerialWorker* worker) {
        worker->setTransportMode(SerialWorker::TransportMode::TypedEvidence);
    });
}

bool TransportRuntime::setTransportModeKey(const QString& key, QString* error) {
    const SerialWorker::TransportMode mode = modeForKey(key);
    return queueWorkerCall(error, "setTransportMode", [mode](SerialWorker* worker) {
        worker->setTransportMode(mode);
    });
}

bool TransportRuntime::startSerial(const QString& portName, QString* error) {
    return queueWorkerCall(error, "startSerial", [portName](SerialWorker* worker) {
        worker->start(portName);
    });
}

bool TransportRuntime::stopSerial(QString* error) {
    return queueWorkerCall(error, "stopSerial", [](SerialWorker* worker) {
        worker->stop();
    });
}

bool TransportRuntime::setTypedStorage(bool enable, const QString& sessionDir, const QJsonObject& metadata, QString* error) {
    return queueWorkerCall(error, "setTypedStorage", [enable, sessionDir, metadata](SerialWorker* worker) {
        worker->setTypedStorage(enable, sessionDir, metadata);
    });
}

bool TransportRuntime::setLegacyLogging(bool enable,
                                        const QString& binPath,
                                        const QString& metaPath,
                                        const QString& rulesSnapshotPath,
                                        const QString& rulesSourcePath,
                                        QString* error) {
    return queueWorkerCall(error, "setLegacyLogging", [enable, binPath, metaPath, rulesSnapshotPath, rulesSourcePath](SerialWorker* worker) {
        worker->setLogging(enable, binPath, metaPath, rulesSnapshotPath, rulesSourcePath);
    });
}

bool TransportRuntime::sendHostFrame(const QByteArray& frame, const QString& summary, QString* error) {
    if (frame.isEmpty()) {
        if (error) *error = QStringLiteral("empty host frame");
        return false;
    }
    return queueWorkerCall(error, "sendHostFrame", [frame, summary](SerialWorker* worker) {
        worker->sendHostFrame(frame, summary);
    });
}

bool TransportRuntime::startControlCycle(int signedCommand,
                                         int rpm,
                                         double steeringDeg,
                                         quint8 motorMode,
                                         quint8 drivingMode,
                                         quint8 bus,
                                         int periodMs,
                                         int frameGapMs,
                                         QString* error) {
    return queueWorkerCall(error, "startControlCycle", [=](SerialWorker* worker) {
        worker->startControlCycle(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus, periodMs, frameGapMs);
    });
}

bool TransportRuntime::updateControlCycle(int signedCommand,
                                          int rpm,
                                          double steeringDeg,
                                          quint8 motorMode,
                                          quint8 drivingMode,
                                          quint8 bus,
                                          QString* error) {
    return queueWorkerCall(error, "updateControlCycle", [=](SerialWorker* worker) {
        worker->updateControlCycle(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus);
    });
}

bool TransportRuntime::stopControlCycle(QString* error) {
    return queueWorkerCall(error, "stopControlCycle", [](SerialWorker* worker) {
        worker->stopControlCycle();
    });
}

bool TransportRuntime::sendControlCycleBurstOnce(int signedCommand,
                                                 int rpm,
                                                 double steeringDeg,
                                                 quint8 motorMode,
                                                 quint8 drivingMode,
                                                 quint8 bus,
                                                 const QString& reason,
                                                 bool resetSlew,
                                                 QString* error) {
    return queueWorkerCall(error, "sendControlCycleBurstOnce", [=](SerialWorker* worker) {
        worker->sendControlCycleBurstOnce(signedCommand, rpm, steeringDeg, motorMode, drivingMode, bus, reason, resetSlew);
    });
}

} // namespace CanMonitorTransport
