#pragma once

#include "core/CoreMaterializedViewStore.h"

#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QVector>

class QLocalSocket;

namespace CanMonitorCore {

class CoreIpcServerRuntime : public QObject {
    Q_OBJECT
public:
    explicit CoreIpcServerRuntime(CoreMaterializedViewStore* viewStore, QObject* parent = nullptr);
    ~CoreIpcServerRuntime() override;

    bool listen(const QString& serverName, QString* errorOut = nullptr);
    void close();
    bool isListening() const;
    QString serverName() const;

    void publishViewChanged(const ViewChanged& change);
    void publishHostFrameWriteResult(quint64 requestId, bool ok, const QString& summary, quint64 bytesWritten);

signals:
    void clientConnected();
    void clientDisconnected();
    void protocolError(const QString& error);
    void hostFrameRequested(quint64 requestId, const QByteArray& frame, const QString& summary);

private:
    void acceptConnection();
    void readClient(QLocalSocket* socket);
    void removeClient(QLocalSocket* socket);
    void handleMessage(QLocalSocket* socket, const QJsonObject& message);
    void sendObject(QLocalSocket* socket, const QJsonObject& object);
    QJsonObject baseResponse(const QJsonObject& request, const QString& messageType) const;
    QJsonObject errorResponse(const QJsonObject& request, const QString& code, const QString& detail) const;

    CoreMaterializedViewStore* m_viewStore = nullptr;
    QLocalServer m_server;
    QVector<QPointer<QLocalSocket>> m_clients;
    QHash<QLocalSocket*, QByteArray> m_buffers;
};

} // namespace CanMonitorCore
