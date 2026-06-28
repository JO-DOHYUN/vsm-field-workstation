#pragma once

#include "core/CoreMaterializedViewStore.h"

#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QVector>
#include <QJsonObject>

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
    QJsonObject statusJson() const;

    void publishViewChanged(const ViewChanged& change);
    void publishHostFrameWriteResult(quint64 requestId, bool ok, const QString& summary, quint64 bytesWritten);
    void publishCaptureStorageUpdate(quint64 requestId,
                                     bool ok,
                                     const QString& error,
                                     bool stateChanged,
                                     bool active,
                                     const QString& path,
                                     bool progressDue,
                                     quint64 bytesWritten,
                                     quint64 recordCount);

signals:
    void clientConnected();
    void clientDisconnected();
    void protocolError(const QString& error);
    void transportStartRequested(quint64 requestId, const QString& mode, const QString& endpoint);
    void transportStopRequested(quint64 requestId);
    void hostFrameRequested(quint64 requestId, const QByteArray& frame, const QString& summary);
    void controlCycleRequested(quint64 requestId, const QString& action, const QJsonObject& payload);
    void captureStartRequested(quint64 requestId, const QString& sessionDir, const QJsonObject& metadata);
    void captureStopRequested(quint64 requestId, const QString& inactivePath, const QJsonObject& diagnostics);
    void analysisModelRequested(quint64 requestId, const QString& modelPath, bool modelEnabled);

private:
    void acceptConnection();
    void readClient(QLocalSocket* socket);
    void removeClient(QLocalSocket* socket);
    void handleMessage(QLocalSocket* socket, const QJsonObject& message);
    void flushPendingViewChanges();
    void sendObject(QLocalSocket* socket, const QJsonObject& object);
    QJsonObject baseResponse(const QJsonObject& request, const QString& messageType) const;
    QJsonObject errorResponse(const QJsonObject& request, const QString& code, const QString& detail) const;

    CoreMaterializedViewStore* m_viewStore = nullptr;
    QLocalServer m_server;
    QVector<QPointer<QLocalSocket>> m_clients;
    QHash<QLocalSocket*, QByteArray> m_buffers;
    QHash<int, ViewChanged> m_pendingViewChanges;
    bool m_viewFlushScheduled = false;
    quint64 m_publishedViewNotifications = 0;
    quint64 m_coalescedViewNotifications = 0;
    quint64 m_droppedViewNotifications = 0;
    quint64 m_snapshotResponses = 0;
    quint64 m_droppedSnapshotResponses = 0;
    quint64 m_disconnectedSlowClients = 0;
    qint64 m_maxQueuedBytes = 0;
    qint64 m_maxSnapshotResponseBytes = 0;
};

} // namespace CanMonitorCore
