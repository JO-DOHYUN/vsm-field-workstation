#include "core/CoreIpcServerRuntime.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QTimer>
#include <QDateTime>

#include <algorithm>
#include <utility>

namespace CanMonitorCore {

namespace {
constexpr qint64 kIpcViewChangedCoalesceMs = 50;
constexpr qint64 kIpcViewBackpressureBytes = 2 * 1024 * 1024;
constexpr qint64 kIpcCriticalBackpressureBytes = 4 * 1024 * 1024;
constexpr qint64 kIpcMaxSnapshotResponseBytes = 2 * 1024 * 1024;
constexpr qsizetype kIpcMaxHostFrameBytes = 8192;

qint64 viewPublishIntervalMs(CoreViewName viewName, CoreViewSeverity severity) {
    if (severity == CoreViewSeverity::Fatal || severity == CoreViewSeverity::Error) {
        return kIpcViewChangedCoalesceMs;
    }
    switch (viewName) {
    case CoreViewName::LiveLatest:
        return 50;  // 20 Hz display view ceiling.
    case CoreViewName::CoreHealth:
    case CoreViewName::FatalDiagnostics:
    case CoreViewName::ControlAudit:
        return 100;
    case CoreViewName::TransportSummary:
    case CoreViewName::CaptureProgress:
    case CoreViewName::AnalysisSnapshot:
    case CoreViewName::RawLedgerTail:
    case CoreViewName::GraphBucket:
        return 250;
    }
    return 250;
}

bool intInRange(const QJsonObject& object, const QString& key, int minValue, int maxValue, bool required, QString* errorOut) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        if (required && errorOut) *errorOut = QStringLiteral("missing_%1").arg(key);
        return !required;
    }
    if (!value.isDouble()) {
        if (errorOut) *errorOut = QStringLiteral("invalid_%1").arg(key);
        return false;
    }
    const int parsed = value.toInt();
    if (parsed < minValue || parsed > maxValue) {
        if (errorOut) *errorOut = QStringLiteral("out_of_range_%1").arg(key);
        return false;
    }
    return true;
}

bool doubleInRange(const QJsonObject& object, const QString& key, double minValue, double maxValue, bool required, QString* errorOut) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        if (required && errorOut) *errorOut = QStringLiteral("missing_%1").arg(key);
        return !required;
    }
    if (!value.isDouble()) {
        if (errorOut) *errorOut = QStringLiteral("invalid_%1").arg(key);
        return false;
    }
    const double parsed = value.toDouble();
    if (parsed < minValue || parsed > maxValue) {
        if (errorOut) *errorOut = QStringLiteral("out_of_range_%1").arg(key);
        return false;
    }
    return true;
}

bool validateControlPayload(const QString& action, const QJsonObject& payload, QString* errorOut) {
    if (action == QStringLiteral("stop")) return true;
    const bool cycleStart = action == QStringLiteral("start");
    return intInRange(payload, QStringLiteral("signed_command"), -100000, 100000, true, errorOut) &&
           intInRange(payload, QStringLiteral("rpm"), -100000, 100000, true, errorOut) &&
           doubleInRange(payload, QStringLiteral("steering_deg"), -1080.0, 1080.0, true, errorOut) &&
           intInRange(payload, QStringLiteral("motor_mode"), 0, 255, true, errorOut) &&
           intInRange(payload, QStringLiteral("driving_mode"), 0, 255, true, errorOut) &&
           intInRange(payload, QStringLiteral("bus"), 0, 255, true, errorOut) &&
           intInRange(payload, QStringLiteral("period_ms"), 5, 5000, cycleStart, errorOut) &&
           intInRange(payload, QStringLiteral("frame_gap_ms"), 0, 5000, cycleStart, errorOut);
}
}

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
    m_pendingViewChanges.clear();
    m_lastPublishedViewMs.clear();
    m_viewFlushScheduled = false;
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

QJsonObject CoreIpcServerRuntime::statusJson() const {
    return QJsonObject{{QStringLiteral("ipc_view_published"), QString::number(m_publishedViewNotifications)},
                       {QStringLiteral("ipc_view_coalesced"), QString::number(m_coalescedViewNotifications)},
                       {QStringLiteral("ipc_view_deferred"), QString::number(m_deferredViewNotifications)},
                       {QStringLiteral("ipc_view_dropped"), QString::number(m_droppedViewNotifications)},
                       {QStringLiteral("ipc_snapshot_responses"), QString::number(m_snapshotResponses)},
                       {QStringLiteral("ipc_snapshot_dropped"), QString::number(m_droppedSnapshotResponses)},
                       {QStringLiteral("ipc_snapshot_max_bytes"), QString::number(m_maxSnapshotResponseBytes)},
                       {QStringLiteral("ipc_slow_client_disconnects"), QString::number(m_disconnectedSlowClients)},
                       {QStringLiteral("ipc_max_queued_bytes"), QString::number(m_maxQueuedBytes)},
                       {QStringLiteral("ipc_pending_views"), m_pendingViewChanges.size()},
                       {QStringLiteral("ipc_clients"), m_clients.size()}};
}

void CoreIpcServerRuntime::publishViewChanged(const ViewChanged& change) {
    const int key = static_cast<int>(change.viewName);
    if (m_pendingViewChanges.contains(key)) {
        ++m_coalescedViewNotifications;
    }
    m_pendingViewChanges.insert(key, change);
    scheduleViewChangeFlush(kIpcViewChangedCoalesceMs);
}

void CoreIpcServerRuntime::scheduleViewChangeFlush(qint64 delayMs) {
    if (m_viewFlushScheduled) return;
    m_viewFlushScheduled = true;
    QTimer::singleShot(std::max<qint64>(1, delayMs), this, &CoreIpcServerRuntime::flushPendingViewChanges);
}

void CoreIpcServerRuntime::flushPendingViewChanges() {
    m_viewFlushScheduled = false;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QHash<int, ViewChanged> ready;
    qint64 nextDelayMs = -1;
    for (auto it = m_pendingViewChanges.begin(); it != m_pendingViewChanges.end();) {
        const ViewChanged& change = it.value();
        const int key = it.key();
        const qint64 intervalMs = viewPublishIntervalMs(change.viewName, change.severity);
        const qint64 lastPublishedMs = m_lastPublishedViewMs.value(key, 0);
        const bool due = lastPublishedMs <= 0 || (nowMs - lastPublishedMs) >= intervalMs;
        if (due) {
            ready.insert(key, change);
            it = m_pendingViewChanges.erase(it);
            continue;
        }
        const qint64 remainingMs = std::max<qint64>(1, intervalMs - (nowMs - lastPublishedMs));
        nextDelayMs = nextDelayMs < 0 ? remainingMs : std::min(nextDelayMs, remainingMs);
        ++m_deferredViewNotifications;
        ++it;
    }

    for (auto it = ready.cbegin(); it != ready.cend(); ++it) {
        const ViewChanged& change = it.value();
        const QJsonObject message{{QStringLiteral("message_type"), QStringLiteral("view_changed")},
                                  {QStringLiteral("change"), change.toJson()}};
        ++m_publishedViewNotifications;
        m_lastPublishedViewMs.insert(it.key(), nowMs);
        for (const auto& client : std::as_const(m_clients)) {
            if (client) {
                sendObject(client, message);
            }
        }
    }
    if (!m_pendingViewChanges.isEmpty()) {
        scheduleViewChangeFlush(nextDelayMs > 0 ? nextDelayMs : kIpcViewChangedCoalesceMs);
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

void CoreIpcServerRuntime::publishCaptureStorageUpdate(quint64 requestId,
                                                       bool ok,
                                                       const QString& error,
                                                       bool stateChanged,
                                                       bool active,
                                                       const QString& path,
                                                       bool progressDue,
                                                       quint64 bytesWritten,
                                                       quint64 recordCount) {
    QJsonObject message{{QStringLiteral("message_type"), QStringLiteral("capture_storage_update")},
                        {QStringLiteral("request_id"), QString::number(requestId)},
                        {QStringLiteral("ok"), ok},
                        {QStringLiteral("error"), error},
                        {QStringLiteral("state_changed"), stateChanged},
                        {QStringLiteral("active"), active},
                        {QStringLiteral("path"), path},
                        {QStringLiteral("progress_due"), progressDue},
                        {QStringLiteral("bytes_written"), QString::number(bytesWritten)},
                        {QStringLiteral("record_count"), QString::number(recordCount)}};
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
    if (type == QStringLiteral("start_transport")) {
        const quint64 requestId = message.value(QStringLiteral("request_id")).toVariant().toULongLong();
        const QString mode = message.value(QStringLiteral("mode")).toString();
        const QString endpoint = message.value(QStringLiteral("endpoint")).toString().trimmed();
        if (endpoint.isEmpty()) {
            sendObject(socket, errorResponse(message, QStringLiteral("empty_transport_endpoint"), QString()));
            return;
        }
        if (endpoint.size() > 512) {
            sendObject(socket, errorResponse(message, QStringLiteral("transport_endpoint_too_long"), QString::number(endpoint.size())));
            return;
        }
        if (mode != QStringLiteral("serial") && mode != QStringLiteral("gateway_tcp")) {
            sendObject(socket, errorResponse(message, QStringLiteral("invalid_transport_mode"), mode));
            return;
        }
        emit transportStartRequested(requestId, mode, endpoint);
        return;
    }
    if (type == QStringLiteral("stop_transport")) {
        const quint64 requestId = message.value(QStringLiteral("request_id")).toVariant().toULongLong();
        emit transportStopRequested(requestId);
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
        if (frame.size() > kIpcMaxHostFrameBytes) {
            sendObject(socket, errorResponse(message, QStringLiteral("host_frame_too_large"), QString::number(frame.size())));
            return;
        }
        emit hostFrameRequested(requestId, frame, summary);
        return;
    }
    if (type == QStringLiteral("control_cycle")) {
        const quint64 requestId = message.value(QStringLiteral("request_id")).toVariant().toULongLong();
        const QString action = message.value(QStringLiteral("action")).toString();
        if (action != QStringLiteral("start") &&
            action != QStringLiteral("update") &&
            action != QStringLiteral("stop") &&
            action != QStringLiteral("burst_once")) {
            sendObject(socket, errorResponse(message, QStringLiteral("invalid_control_cycle_action"), action));
            return;
        }
        const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
        QString validationError;
        if (!validateControlPayload(action, payload, &validationError)) {
            sendObject(socket, errorResponse(message, QStringLiteral("invalid_control_cycle_payload"), validationError));
            return;
        }
        emit controlCycleRequested(requestId, action, payload);
        return;
    }
    if (type == QStringLiteral("start_capture")) {
        const quint64 requestId = message.value(QStringLiteral("request_id")).toVariant().toULongLong();
        const QString sessionDir = message.value(QStringLiteral("session_dir")).toString();
        if (sessionDir.trimmed().isEmpty()) {
            sendObject(socket, errorResponse(message, QStringLiteral("empty_capture_session_dir"), QString()));
            return;
        }
        if (sessionDir.size() > 1024) {
            sendObject(socket, errorResponse(message, QStringLiteral("capture_session_dir_too_long"), QString::number(sessionDir.size())));
            return;
        }
        emit captureStartRequested(requestId, sessionDir, message.value(QStringLiteral("metadata")).toObject());
        return;
    }
    if (type == QStringLiteral("stop_capture")) {
        const quint64 requestId = message.value(QStringLiteral("request_id")).toVariant().toULongLong();
        emit captureStopRequested(requestId,
                                  message.value(QStringLiteral("inactive_path")).toString(),
                                  message.value(QStringLiteral("diagnostics")).toObject());
        return;
    }
    if (type == QStringLiteral("set_analysis_model")) {
        const quint64 requestId = message.value(QStringLiteral("request_id")).toVariant().toULongLong();
        emit analysisModelRequested(requestId,
                                    message.value(QStringLiteral("model_path")).toString(),
                                    message.value(QStringLiteral("model_enabled")).toBool(true));
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
    const qint64 responseBytes = QJsonDocument(response).toJson(QJsonDocument::Compact).size();
    ++m_snapshotResponses;
    m_maxSnapshotResponseBytes = std::max(m_maxSnapshotResponseBytes, responseBytes);
    if (responseBytes > kIpcMaxSnapshotResponseBytes) {
        ++m_droppedSnapshotResponses;
        sendObject(socket,
                   errorResponse(message,
                                 QStringLiteral("view_snapshot_too_large"),
                                 QString::number(responseBytes)));
        return;
    }
    sendObject(socket, response);
}

void CoreIpcServerRuntime::sendObject(QLocalSocket* socket, const QJsonObject& object) {
    if (!socket) return;
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const qint64 queued = socket->bytesToWrite();
    const qint64 projectedQueued = queued + payload.size() + 1;
    m_maxQueuedBytes = std::max(m_maxQueuedBytes, projectedQueued);
    const bool viewNotification = object.value(QStringLiteral("message_type")).toString() == QStringLiteral("view_changed");
    if (viewNotification && projectedQueued > kIpcViewBackpressureBytes) {
        ++m_droppedViewNotifications;
        return;
    }
    if (!viewNotification && projectedQueued > kIpcCriticalBackpressureBytes) {
        ++m_disconnectedSlowClients;
        emit protocolError(QStringLiteral("ipc critical response backpressure exceeded"));
        socket->disconnectFromServer();
        return;
    }
    socket->write(payload);
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
