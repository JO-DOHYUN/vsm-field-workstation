#pragma once

#include <QHash>
#include <QLocalSocket>
#include <QObject>
#include <QJsonObject>

namespace CanMonitorCore {

class CoreIpcClientRuntime : public QObject {
    Q_OBJECT
public:
    explicit CoreIpcClientRuntime(QObject* parent = nullptr);

    void connectToServer(const QString& serverName);
    void disconnectFromServer();
    bool isConnected() const;
    quint64 ping();
    quint64 requestView(const QString& viewName, quint64 sinceSeq, int limit);
    bool requestViewWithId(quint64 requestId, const QString& viewName, quint64 sinceSeq, int limit);
    quint64 startTransport(const QString& mode, const QString& endpoint);
    quint64 stopTransport();
    quint64 sendHostFrame(const QByteArray& frame, const QString& summary);
    quint64 startControlCycle(int signedCommand,
                              int rpm,
                              double steeringDeg,
                              quint8 motorMode,
                              quint8 drivingMode,
                              quint8 bus,
                              int periodMs,
                              int frameGapMs);
    quint64 updateControlCycle(int signedCommand,
                               int rpm,
                               double steeringDeg,
                               quint8 motorMode,
                               quint8 drivingMode,
                               quint8 bus);
    quint64 stopControlCycle();
    quint64 sendControlCycleBurstOnce(int signedCommand,
                                      int rpm,
                                      double steeringDeg,
                                      quint8 motorMode,
                                      quint8 drivingMode,
                                      quint8 bus,
                                      const QString& reason,
                                      bool resetSlew);
    quint64 startCapture(const QString& sessionDir, const QJsonObject& metadata);
    quint64 stopCapture(const QString& inactivePath, const QJsonObject& diagnostics);
    quint64 setAnalysisModel(const QString& modelPath, bool modelEnabled);

signals:
    void connectedChanged(bool connected);
    void pongReceived(quint64 requestId);
    void viewChanged(const QJsonObject& change);
    void viewSnapshotReceived(quint64 requestId, bool changed, const QJsonObject& snapshot, const QJsonObject& change);
    void hostFrameWriteResult(quint64 requestId, bool ok, const QString& summary, quint64 bytesWritten);
    void captureStorageUpdate(quint64 requestId,
                              bool ok,
                              const QString& error,
                              bool stateChanged,
                              bool active,
                              const QString& path,
                              bool progressDue,
                              quint64 bytesWritten,
                              quint64 recordCount);
    void errorReceived(quint64 requestId, const QString& error, const QString& detail);

private:
    void readMessages();
    void handleMessage(const QJsonObject& message);
    quint64 sendMessage(QJsonObject message);
    bool sendMessageWithId(quint64 requestId, QJsonObject message);

    QLocalSocket m_socket;
    QByteArray m_buffer;
    quint64 m_nextRequestId = 1;
};

} // namespace CanMonitorCore
