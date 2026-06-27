#include "core/CoreIpcClientRuntime.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

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

bool CoreIpcClientRuntime::requestViewWithId(quint64 requestId, const QString& viewName, quint64 sinceSeq, int limit) {
    return sendMessageWithId(requestId,
                             QJsonObject{{QStringLiteral("message_type"), QStringLiteral("get_view")},
                                         {QStringLiteral("view_name"), viewName},
                                         {QStringLiteral("since_seq"), QString::number(sinceSeq)},
                                         {QStringLiteral("limit"), limit}});
}

quint64 CoreIpcClientRuntime::startTransport(const QString& mode, const QString& endpoint) {
    return sendMessage(QJsonObject{{QStringLiteral("message_type"), QStringLiteral("start_transport")},
                                   {QStringLiteral("mode"), mode},
                                   {QStringLiteral("endpoint"), endpoint}});
}

quint64 CoreIpcClientRuntime::stopTransport() {
    return sendMessage(QJsonObject{{QStringLiteral("message_type"), QStringLiteral("stop_transport")}});
}

quint64 CoreIpcClientRuntime::sendHostFrame(const QByteArray& frame, const QString& summary) {
    return sendMessage(QJsonObject{{QStringLiteral("message_type"), QStringLiteral("host_frame")},
                                   {QStringLiteral("frame_base64"), QString::fromLatin1(frame.toBase64())},
                                   {QStringLiteral("summary"), summary}});
}

quint64 CoreIpcClientRuntime::startCapture(const QString& sessionDir, const QJsonObject& metadata) {
    return sendMessage(QJsonObject{{QStringLiteral("message_type"), QStringLiteral("start_capture")},
                                   {QStringLiteral("session_dir"), sessionDir},
                                   {QStringLiteral("metadata"), metadata}});
}

quint64 CoreIpcClientRuntime::stopCapture(const QString& inactivePath, const QJsonObject& diagnostics) {
    return sendMessage(QJsonObject{{QStringLiteral("message_type"), QStringLiteral("stop_capture")},
                                   {QStringLiteral("inactive_path"), inactivePath},
                                   {QStringLiteral("diagnostics"), diagnostics}});
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
    if (type == QStringLiteral("host_frame_write_result")) {
        emit hostFrameWriteResult(requestId,
                                  message.value(QStringLiteral("ok")).toBool(),
                                  message.value(QStringLiteral("summary")).toString(),
                                  message.value(QStringLiteral("bytes_written")).toVariant().toULongLong());
        return;
    }
    if (type == QStringLiteral("capture_storage_update")) {
        emit captureStorageUpdate(requestId,
                                  message.value(QStringLiteral("ok")).toBool(),
                                  message.value(QStringLiteral("error")).toString(),
                                  message.value(QStringLiteral("state_changed")).toBool(),
                                  message.value(QStringLiteral("active")).toBool(),
                                  message.value(QStringLiteral("path")).toString(),
                                  message.value(QStringLiteral("progress_due")).toBool(),
                                  message.value(QStringLiteral("bytes_written")).toVariant().toULongLong(),
                                  message.value(QStringLiteral("record_count")).toVariant().toULongLong());
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
    sendMessageWithId(requestId, std::move(message));
    return requestId;
}

bool CoreIpcClientRuntime::sendMessageWithId(quint64 requestId, QJsonObject message) {
    if (!isConnected()) {
        emit errorReceived(requestId, QStringLiteral("not_connected"), QString());
        return false;
    }
    message.insert(QStringLiteral("schema_version"), 1);
    message.insert(QStringLiteral("request_id"), QString::number(requestId));
    m_socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact));
    m_socket.write("\n");
    m_socket.flush();
    return true;
}

} // namespace CanMonitorCore
