#include "core/CoreIpcServerRuntime.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#include <algorithm>
#include <utility>

namespace CanMonitorCore {

CoreIpcServerRuntime::CoreIpcServerRuntime(CoreMaterializedViewStore* viewStore, QObject* parent)
    : QObject(parent)
    , m_viewStore(viewStore) {
    connect(&m_server, &QLocalServer::newConnection, this, &CoreIpcServerRuntime::acceptConnection);
}

CoreIpcServerRuntime::~CoreIpcServerRuntime() {
    close();
}

bool CoreIpcServerRuntime::listen(const QString& serverName, QString* errorOut) {
    close();
    QLocalServer::removeServer(serverName);
    if (!m_server.listen(serverName)) {
        if (errorOut) *errorOut = m_server.errorString();
        return false;
    }
    return true;
}

void CoreIpcServerRuntime::close() {
    for (const auto& client : std::as_const(m_clients)) {
        if (client) {
            client->disconnect(this);
            client->disconnectFromServer();
            client->deleteLater();
        }
    }
    m_clients.clear();
    m_buffers.clear();
    if (m_server.isListening()) {
        const QString name = m_server.serverName();
        m_server.close();
        if (!name.isEmpty()) {
            QLocalServer::removeServer(name);
        }
    }
}

bool CoreIpcServerRuntime::isListening() const {
    return m_server.isListening();
}

QString CoreIpcServerRuntime::serverName() const {
    return m_server.serverName();
}

void CoreIpcServerRuntime::publishViewChanged(const ViewChanged& change) {
    const QJsonObject message{{QStringLiteral("message_type"), QStringLiteral("view_changed")},
                              {QStringLiteral("change"), change.toJson()}};
    for (const auto& client : std::as_const(m_clients)) {
        if (client) {
            sendObject(client, message);
        }
    }
}

void CoreIpcServerRuntime::publishHostFrameWriteResult(quint64 requestId, bool ok, const QString& summary, quint64 bytesWritten) {
    QJsonObject message{{QStringLiteral("message_type"), QStringLiteral("host_frame_write_result")},
                        {QStringLiteral("request_id"), QString::number(requestId)},
                        {QStringLiteral("ok"), ok},
                        {QStringLiteral("summary"), summary},
                        {QStringLiteral("bytes_written"), QString::number(bytesWritten)}};
    for (const auto& client : std::as_const(m_clients)) {
        if (client) {
            sendObject(client, message);
        }
    }
}

void CoreIpcServerRuntime::acceptConnection() {
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
        m_clients.push_back(socket);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { readClient(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() { removeClient(socket); });
        emit clientConnected();
    }
}

void CoreIpcServerRuntime::readClient(QLocalSocket* socket) {
    if (!socket) return;
    QByteArray& buffer = m_buffers[socket];
    buffer += socket->readAll();
    constexpr qsizetype kMaxBufferedBytes = 1024 * 1024;
    if (buffer.size() > kMaxBufferedBytes) {
        emit protocolError(QStringLiteral("ipc client buffer exceeded"));
        sendObject(socket, QJsonObject{{QStringLiteral("message_type"), QStringLiteral("error")},
                                       {QStringLiteral("error"), QStringLiteral("buffer_exceeded")}});
        socket->disconnectFromServer();
        return;
    }

    for (;;) {
        const qsizetype newline = buffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit protocolError(QStringLiteral("invalid ipc json"));
            sendObject(socket, QJsonObject{{QStringLiteral("message_type"), QStringLiteral("error")},
                                           {QStringLiteral("error"), QStringLiteral("invalid_json")}});
            continue;
        }
        handleMessage(socket, document.object());
    }
}

void CoreIpcServerRuntime::removeClient(QLocalSocket* socket) {
    m_buffers.remove(socket);
    m_clients.erase(std::remove_if(m_clients.begin(),
                                   m_clients.end(),
                                   [socket](const QPointer<QLocalSocket>& client) {
                                       return client.isNull() || client.data() == socket;
                                   }),
                    m_clients.end());
    if (socket) socket->deleteLater();
    emit clientDisconnected();
}

void CoreIpcServerRuntime::handleMessage(QLocalSocket* socket, const QJsonObject& message) {
    const QString type = message.value(QStringLiteral("message_type")).toString();
    if (type == QStringLiteral("ping")) {
        sendObject(socket, baseResponse(message, QStringLiteral("pong")));
        return;
    }
    if (type == QStringLiteral("host_frame")) {
        const quint64 requestId = message.value(QStringLiteral("request_id")).toVariant().toULongLong();
        const QByteArray frame = QByteArray::fromBase64(message.value(QStringLiteral("frame_base64")).toString().toLatin1());
        const QString summary = message.value(QStringLiteral("summary")).toString();
        if (frame.isEmpty()) {
            sendObject(socket, errorResponse(message, QStringLiteral("empty_host_frame"), summary));
            return;
        }
        emit hostFrameRequested(requestId, frame, summary);
        return;
    }
    if (type != QStringLiteral("get_view")) {
        sendObject(socket, errorResponse(message, QStringLiteral("unknown_message"), type));
        return;
    }
    if (!m_viewStore) {
        sendObject(socket, errorResponse(message, QStringLiteral("view_store_unavailable"), QString()));
        return;
    }

    CoreViewName viewName = CoreViewName::CoreHealth;
    if (!coreViewNameFromString(message.value(QStringLiteral("view_name")).toString(), &viewName)) {
        sendObject(socket, errorResponse(message, QStringLiteral("unknown_view"), message.value(QStringLiteral("view_name")).toString()));
        return;
    }

    const quint64 sinceSeq = message.value(QStringLiteral("since_seq")).toVariant().toULongLong();
    const int limit = message.value(QStringLiteral("limit")).toInt();
    const auto result = m_viewStore->queryView({viewName, sinceSeq, limit});

    QJsonObject response = baseResponse(message, QStringLiteral("view_snapshot"));
    response.insert(QStringLiteral("changed"), result.changed);
    response.insert(QStringLiteral("change"), result.changed ? result.change.toJson() : QJsonObject{{QStringLiteral("view_name"), coreViewNameToString(viewName)},
                                                                                                    {QStringLiteral("view_seq"), QString::number(sinceSeq)}});
    response.insert(QStringLiteral("snapshot"), result.changed ? result.snapshot.toJson() : QJsonObject{});
    sendObject(socket, response);
}

void CoreIpcServerRuntime::sendObject(QLocalSocket* socket, const QJsonObject& object) {
    if (!socket) return;
    socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

QJsonObject CoreIpcServerRuntime::baseResponse(const QJsonObject& request, const QString& messageType) const {
    QJsonObject response;
    response.insert(QStringLiteral("message_type"), messageType);
    response.insert(QStringLiteral("request_id"),
                    QString::number(request.value(QStringLiteral("request_id")).toVariant().toULongLong()));
    response.insert(QStringLiteral("schema_version"), 1);
    return response;
}

QJsonObject CoreIpcServerRuntime::errorResponse(const QJsonObject& request, const QString& code, const QString& detail) const {
    QJsonObject response = baseResponse(request, QStringLiteral("error"));
    response.insert(QStringLiteral("error"), code);
    if (!detail.isEmpty()) response.insert(QStringLiteral("detail"), detail);
    return response;
}

} // namespace CanMonitorCore
