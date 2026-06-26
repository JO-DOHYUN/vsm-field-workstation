#include "core/CoreIpcClientRuntime.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace CanMonitorCore {

CoreIpcClientRuntime::CoreIpcClientRuntime(QObject* parent)
    : QObject(parent) {
    connect(&m_socket, &QLocalSocket::connected, this, [this]() { emit connectedChanged(true); });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this]() { emit connectedChanged(false); });
    connect(&m_socket, &QLocalSocket::readyRead, this, &CoreIpcClientRuntime::readMessages);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        emit errorReceived(0, QStringLiteral("socket_error"), m_socket.errorString());
    });
}

void CoreIpcClientRuntime::connectToServer(const QString& serverName) {
    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        m_socket.abort();
    }
    m_buffer.clear();
    m_socket.connectToServer(serverName);
}

void CoreIpcClientRuntime::disconnectFromServer() {
    m_socket.disconnectFromServer();
}

bool CoreIpcClientRuntime::isConnected() const {
    return m_socket.state() == QLocalSocket::ConnectedState;
}

quint64 CoreIpcClientRuntime::ping() {
    return sendMessage(QJsonObject{{QStringLiteral("message_type"), QStringLiteral("ping")}});
}

quint64 CoreIpcClientRuntime::requestView(const QString& viewName, quint64 sinceSeq, int limit) {
    return sendMessage(QJsonObject{{QStringLiteral("message_type"), QStringLiteral("get_view")},
                                   {QStringLiteral("view_name"), viewName},
                                   {QStringLiteral("since_seq"), QString::number(sinceSeq)},
                                   {QStringLiteral("limit"), limit}});
}

void CoreIpcClientRuntime::readMessages() {
    m_buffer += m_socket.readAll();
    constexpr qsizetype kMaxBufferedBytes = 1024 * 1024;
    if (m_buffer.size() > kMaxBufferedBytes) {
        emit errorReceived(0, QStringLiteral("buffer_exceeded"), QString());
        m_socket.abort();
        return;
    }

    for (;;) {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit errorReceived(0, QStringLiteral("invalid_json"), QString());
            continue;
        }
        handleMessage(document.object());
    }
}

void CoreIpcClientRuntime::handleMessage(const QJsonObject& message) {
    const QString type = message.value(QStringLiteral("message_type")).toString();
    const quint64 requestId = message.value(QStringLiteral("request_id")).toVariant().toULongLong();
    if (type == QStringLiteral("pong")) {
        emit pongReceived(requestId);
        return;
    }
    if (type == QStringLiteral("view_changed")) {
        emit viewChanged(message.value(QStringLiteral("change")).toObject());
        return;
    }
    if (type == QStringLiteral("view_snapshot")) {
        emit viewSnapshotReceived(requestId,
                                  message.value(QStringLiteral("changed")).toBool(),
                                  message.value(QStringLiteral("snapshot")).toObject(),
                                  message.value(QStringLiteral("change")).toObject());
        return;
    }
    if (type == QStringLiteral("error")) {
        emit errorReceived(requestId,
                           message.value(QStringLiteral("error")).toString(),
                           message.value(QStringLiteral("detail")).toString());
        return;
    }
    emit errorReceived(requestId, QStringLiteral("unknown_message"), type);
}

quint64 CoreIpcClientRuntime::sendMessage(QJsonObject message) {
    const quint64 requestId = m_nextRequestId++;
    message.insert(QStringLiteral("schema_version"), 1);
    message.insert(QStringLiteral("request_id"), QString::number(requestId));
    m_socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact));
    m_socket.write("\n");
    m_socket.flush();
    return requestId;
}

} // namespace CanMonitorCore
