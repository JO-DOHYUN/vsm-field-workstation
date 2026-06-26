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

signals:
    void connectedChanged(bool connected);
    void pongReceived(quint64 requestId);
    void viewChanged(const QJsonObject& change);
    void viewSnapshotReceived(quint64 requestId, bool changed, const QJsonObject& snapshot, const QJsonObject& change);
    void errorReceived(quint64 requestId, const QString& error, const QString& detail);

private:
    void readMessages();
    void handleMessage(const QJsonObject& message);
    quint64 sendMessage(QJsonObject message);

    QLocalSocket m_socket;
    QByteArray m_buffer;
    quint64 m_nextRequestId = 1;
};

} // namespace CanMonitorCore
