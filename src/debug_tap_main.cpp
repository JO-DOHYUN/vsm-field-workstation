#include "backend/BuildMetadata.h"
#include "backend/core/CoreIpcClientRuntime.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

struct ViewSpec {
    QString name;
    int limit = 0;
    bool periodic = true;
};

QJsonObject buildInfoJson() {
    return QJsonObject::fromVariantMap(BuildMetadata::toVariantMap(BuildMetadata::current()));
}

void writeStdoutJson(const QJsonObject& object) {
    QTextStream stream(stdout);
    stream << QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)) << Qt::endl;
}

bool writeJsonFile(const QString& path, const QJsonObject& object) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

quint64 jsonU64(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (value.isString()) return value.toString().toULongLong();
    if (value.isDouble()) return quint64(value.toDouble());
    return 0;
}

class DebugTapRuntime final : public QObject {
public:
    DebugTapRuntime(QString serverName,
                    QString outDir,
                    QString stopFile,
                    QVector<ViewSpec> views,
                    QObject* parent = nullptr)
        : QObject(parent)
        , m_serverName(std::move(serverName))
        , m_outDir(std::move(outDir))
        , m_stopFile(std::move(stopFile))
        , m_views(std::move(views)) {
        m_tracePath = QDir(m_outDir).filePath(QStringLiteral("debug_tap_trace.jsonl"));
        m_readyPath = QDir(m_outDir).filePath(QStringLiteral("debug_tap.ready.json"));
        m_summaryPath = QDir(m_outDir).filePath(QStringLiteral("debug_tap_summary.json"));

        connect(&m_client, &CanMonitorCore::CoreIpcClientRuntime::connectedChanged,
                this, &DebugTapRuntime::onConnectedChanged);
        connect(&m_client, &CanMonitorCore::CoreIpcClientRuntime::viewChanged,
                this, &DebugTapRuntime::onViewChanged);
        connect(&m_client, &CanMonitorCore::CoreIpcClientRuntime::viewSnapshotReceived,
                this, &DebugTapRuntime::onViewSnapshot);
        connect(&m_client, &CanMonitorCore::CoreIpcClientRuntime::errorReceived,
                this, &DebugTapRuntime::onIpcError);

        m_reconnectTimer.setSingleShot(true);
        connect(&m_reconnectTimer, &QTimer::timeout, this, &DebugTapRuntime::connectToCore);

        m_requestFlushTimer.setSingleShot(true);
        connect(&m_requestFlushTimer, &QTimer::timeout, this, &DebugTapRuntime::flushPendingRequests);

        m_periodicTimer.setInterval(1000);
        connect(&m_periodicTimer, &QTimer::timeout, this, &DebugTapRuntime::periodicSnapshot);

        m_stopPollTimer.setInterval(250);
        connect(&m_stopPollTimer, &QTimer::timeout, this, &DebugTapRuntime::pollStopFile);

        m_timeoutTimer.setInterval(500);
        connect(&m_timeoutTimer, &QTimer::timeout, this, &DebugTapRuntime::expireRequests);
    }

    ~DebugTapRuntime() override {
        finalizeSummary(QStringLiteral("destructor"), false);
    }

    bool start(QString* errorOut) {
        if (m_serverName.trimmed().isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("empty core server name");
            return false;
        }
        if (!QDir().mkpath(m_outDir)) {
            if (errorOut) *errorOut = QStringLiteral("cannot create output directory: %1").arg(m_outDir);
            return false;
        }
        m_trace.setFileName(m_tracePath);
        if (!m_trace.open(QIODevice::WriteOnly | QIODevice::Append)) {
            if (errorOut) *errorOut = QStringLiteral("cannot open trace file: %1").arg(m_trace.errorString());
            return false;
        }

        appendTrace(QStringLiteral("tap_start"),
                    QJsonObject{{QStringLiteral("server_name"), m_serverName},
                                {QStringLiteral("out_dir"), m_outDir},
                                {QStringLiteral("build"), buildInfoJson()},
                                {QStringLiteral("views"), viewSpecJson()}});
        writeReady(false);
        connectToCore();
        m_periodicTimer.start();
        m_stopPollTimer.start();
        m_timeoutTimer.start();
        return true;
    }

    QJsonObject summaryJson(const QString& reason) const {
        QJsonObject views;
        for (auto it = m_lastSeq.cbegin(); it != m_lastSeq.cend(); ++it) {
            views.insert(it.key(), QString::number(it.value()));
        }
        return QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-debug-tap")},
                           {QStringLiteral("ok"), m_connectedOnce},
                           {QStringLiteral("reason"), reason},
                           {QStringLiteral("server_name"), m_serverName},
                           {QStringLiteral("out_dir"), m_outDir},
                           {QStringLiteral("trace_path"), m_tracePath},
                           {QStringLiteral("connected_once"), m_connectedOnce},
                           {QStringLiteral("connected"), m_client.isConnected()},
                           {QStringLiteral("view_changed_total"), QString::number(m_viewChangedTotal)},
                           {QStringLiteral("snapshot_total"), QString::number(m_snapshotTotal)},
                           {QStringLiteral("ipc_error_total"), QString::number(m_ipcErrorTotal)},
                           {QStringLiteral("request_timeout_total"), QString::number(m_requestTimeoutTotal)},
                           {QStringLiteral("request_suppressed_total"), QString::number(m_requestSuppressedTotal)},
                           {QStringLiteral("last_view_seq"), views},
                           {QStringLiteral("build"), buildInfoJson()}};
    }

public slots:
    void stop(const QString& reason = QStringLiteral("stop_requested")) {
        if (m_stopping) return;
        m_stopping = true;
        finalizeSummary(reason, true);
        QCoreApplication::quit();
    }

private slots:
    void connectToCore() {
        if (m_stopping || m_client.isConnected()) return;
        ++m_connectAttempts;
        appendTrace(QStringLiteral("ipc_connect_attempt"),
                    QJsonObject{{QStringLiteral("attempt"), QString::number(m_connectAttempts)},
                                {QStringLiteral("server_name"), m_serverName}});
        m_client.connectToServer(m_serverName);
        scheduleReconnect(1000);
    }

    void onConnectedChanged(bool connected) {
        appendTrace(connected ? QStringLiteral("ipc_connected") : QStringLiteral("ipc_disconnected"),
                    QJsonObject{{QStringLiteral("server_name"), m_serverName}});
        if (connected) {
            m_connectedOnce = true;
            writeReady(true);
            writeStdoutJson(QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-debug-tap")},
                                        {QStringLiteral("mode"), QStringLiteral("ready")},
                                        {QStringLiteral("ok"), true},
                                        {QStringLiteral("server_name"), m_serverName},
                                        {QStringLiteral("out_dir"), m_outDir},
                                        {QStringLiteral("trace_path"), m_tracePath}});
            requestAllViews(QStringLiteral("connect"));
        } else if (!m_stopping) {
            scheduleReconnect(500);
        }
    }

    void onViewChanged(const QJsonObject& change) {
        ++m_viewChangedTotal;
        const QString viewName = change.value(QStringLiteral("view_name")).toString();
        const quint64 seq = jsonU64(change, QStringLiteral("view_seq"));
        appendTrace(QStringLiteral("view_changed"),
                    QJsonObject{{QStringLiteral("change"), change}});
        if (!viewName.isEmpty() && seq > m_lastSeq.value(viewName, 0)) {
            m_pendingViews.insert(viewName, QStringLiteral("view_changed"));
            scheduleRequestFlush();
        }
    }

    void onViewSnapshot(quint64 requestId,
                        bool changed,
                        const QJsonObject& snapshot,
                        const QJsonObject& change) {
        const QString viewName = m_requestView.take(requestId);
        m_requestStartedMs.remove(requestId);
        if (!viewName.isEmpty()) {
            m_viewInFlight.remove(viewName);
        }
        if (changed) {
            ++m_snapshotTotal;
            const QJsonObject effectiveChange = !change.isEmpty()
                ? change
                : snapshot;
            const QString changedView = effectiveChange.value(QStringLiteral("view_name")).toString(viewName);
            const quint64 seq = jsonU64(effectiveChange, QStringLiteral("view_seq"));
            if (!changedView.isEmpty() && seq > 0) {
                m_lastSeq.insert(changedView, seq);
            }
        }
        appendTrace(QStringLiteral("view_snapshot"),
                    QJsonObject{{QStringLiteral("request_id"), QString::number(requestId)},
                                {QStringLiteral("view_name"), viewName},
                                {QStringLiteral("changed"), changed},
                                {QStringLiteral("change"), change},
                                {QStringLiteral("snapshot"), snapshot}});
    }

    void onIpcError(quint64 requestId, const QString& error, const QString& detail) {
        ++m_ipcErrorTotal;
        appendTrace(QStringLiteral("ipc_error"),
                    QJsonObject{{QStringLiteral("request_id"), QString::number(requestId)},
                                {QStringLiteral("error"), error},
                                {QStringLiteral("detail"), detail}});
        if (!m_client.isConnected() && !m_stopping) {
            scheduleReconnect(1000);
        }
    }

    void flushPendingRequests() {
        if (!m_client.isConnected() || m_stopping) return;
        const QHash<QString, QString> pending = m_pendingViews;
        m_pendingViews.clear();
        for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
            requestView(it.key(), it.value());
        }
    }

    void periodicSnapshot() {
        appendTrace(QStringLiteral("tap_heartbeat"),
                    QJsonObject{{QStringLiteral("connected"), m_client.isConnected()},
                                {QStringLiteral("view_changed_total"), QString::number(m_viewChangedTotal)},
                                {QStringLiteral("snapshot_total"), QString::number(m_snapshotTotal)},
                                {QStringLiteral("ipc_error_total"), QString::number(m_ipcErrorTotal)},
                                {QStringLiteral("inflight"), m_viewInFlight.size()}});
        writeJsonFile(m_summaryPath, summaryJson(QStringLiteral("heartbeat")));
        if (!m_client.isConnected()) return;
        requestAllViews(QStringLiteral("periodic"));
    }

    void pollStopFile() {
        if (!m_stopFile.isEmpty() && QFileInfo::exists(m_stopFile)) {
            stop(QStringLiteral("stop_file"));
        }
    }

    void expireRequests() {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        QVector<quint64> expired;
        for (auto it = m_requestStartedMs.cbegin(); it != m_requestStartedMs.cend(); ++it) {
            if (nowMs - it.value() > 3000) expired.push_back(it.key());
        }
        for (quint64 requestId : expired) {
            const QString viewName = m_requestView.take(requestId);
            m_requestStartedMs.remove(requestId);
            if (!viewName.isEmpty()) m_viewInFlight.remove(viewName);
            ++m_requestTimeoutTotal;
            appendTrace(QStringLiteral("request_timeout"),
                        QJsonObject{{QStringLiteral("request_id"), QString::number(requestId)},
                                    {QStringLiteral("view_name"), viewName}});
        }
    }

private:
    void requestAllViews(const QString& reason) {
        for (const ViewSpec& spec : std::as_const(m_views)) {
            if (!spec.periodic && reason == QLatin1String("periodic")) continue;
            m_pendingViews.insert(spec.name, reason);
        }
        scheduleRequestFlush();
    }

    void requestView(const QString& viewName, const QString& reason) {
        if (m_viewInFlight.contains(viewName)) {
            ++m_requestSuppressedTotal;
            return;
        }
        const ViewSpec spec = specFor(viewName);
        const quint64 sinceSeq = reason == QLatin1String("periodic")
            ? 0
            : m_lastSeq.value(viewName, 0);
        const quint64 requestId = m_client.requestView(viewName, sinceSeq, spec.limit);
        m_requestView.insert(requestId, viewName);
        m_requestStartedMs.insert(requestId, QDateTime::currentMSecsSinceEpoch());
        m_viewInFlight.insert(viewName, true);
        appendTrace(QStringLiteral("view_request"),
                    QJsonObject{{QStringLiteral("request_id"), QString::number(requestId)},
                                {QStringLiteral("view_name"), viewName},
                                {QStringLiteral("since_seq"), QString::number(sinceSeq)},
                                {QStringLiteral("limit"), spec.limit},
                                {QStringLiteral("reason"), reason}});
    }

    ViewSpec specFor(const QString& viewName) const {
        for (const ViewSpec& spec : m_views) {
            if (spec.name == viewName) return spec;
        }
        return ViewSpec{viewName, 0, true};
    }

    void scheduleRequestFlush() {
        if (!m_requestFlushTimer.isActive()) {
            m_requestFlushTimer.start(100);
        }
    }

    void scheduleReconnect(int delayMs) {
        if (m_stopping || m_client.isConnected()) return;
        if (!m_reconnectTimer.isActive()) {
            m_reconnectTimer.start(std::max(50, delayMs));
        }
    }

    QJsonArray viewSpecJson() const {
        QJsonArray array;
        for (const ViewSpec& spec : m_views) {
            array.append(QJsonObject{{QStringLiteral("view_name"), spec.name},
                                     {QStringLiteral("limit"), spec.limit},
                                     {QStringLiteral("periodic"), spec.periodic}});
        }
        return array;
    }

    void writeReady(bool connected) {
        writeJsonFile(m_readyPath,
                      QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-debug-tap")},
                                  {QStringLiteral("ready"), connected},
                                  {QStringLiteral("server_name"), m_serverName},
                                  {QStringLiteral("out_dir"), m_outDir},
                                  {QStringLiteral("trace_path"), m_tracePath},
                                  {QStringLiteral("summary_path"), m_summaryPath},
                                  {QStringLiteral("views"), viewSpecJson()},
                                  {QStringLiteral("build"), buildInfoJson()}});
    }

    void finalizeSummary(const QString& reason, bool appendStopTrace) {
        if (m_summaryFinalized) {
            return;
        }
        m_summaryFinalized = true;
        const QJsonObject summary = summaryJson(reason);
        if (appendStopTrace && m_trace.isOpen()) {
            appendTrace(QStringLiteral("tap_stop"), summary);
        }
        writeJsonFile(m_summaryPath, summary);
        if (m_trace.isOpen()) {
            m_trace.flush();
        }
    }

    void appendTrace(const QString& event, QJsonObject object) {
        object.insert(QStringLiteral("event"), event);
        object.insert(QStringLiteral("wall_ms"), QString::number(QDateTime::currentMSecsSinceEpoch()));
        object.insert(QStringLiteral("schema_version"), 1);
        m_trace.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        m_trace.write("\n");
        ++m_linesWritten;
        if ((m_linesWritten % 32) == 0) {
            m_trace.flush();
        }
    }

    QString m_serverName;
    QString m_outDir;
    QString m_stopFile;
    QVector<ViewSpec> m_views;
    QString m_tracePath;
    QString m_readyPath;
    QString m_summaryPath;
    QFile m_trace;
    CanMonitorCore::CoreIpcClientRuntime m_client;
    QTimer m_reconnectTimer;
    QTimer m_requestFlushTimer;
    QTimer m_periodicTimer;
    QTimer m_stopPollTimer;
    QTimer m_timeoutTimer;
    QHash<QString, quint64> m_lastSeq;
    QHash<QString, QString> m_pendingViews;
    QHash<QString, bool> m_viewInFlight;
    QHash<quint64, QString> m_requestView;
    QHash<quint64, qint64> m_requestStartedMs;
    quint64 m_connectAttempts = 0;
    quint64 m_viewChangedTotal = 0;
    quint64 m_snapshotTotal = 0;
    quint64 m_ipcErrorTotal = 0;
    quint64 m_requestTimeoutTotal = 0;
    quint64 m_requestSuppressedTotal = 0;
    quint64 m_linesWritten = 0;
    bool m_connectedOnce = false;
    bool m_stopping = false;
    bool m_summaryFinalized = false;
};

QVector<ViewSpec> defaultViews(bool deep) {
    QVector<ViewSpec> views{
        {QStringLiteral("profile_status"), 0, true},
        {QStringLiteral("core_health"), 0, true},
        {QStringLiteral("transport_summary"), 0, true},
        {QStringLiteral("capture_progress"), 0, true},
        {QStringLiteral("fatal_diagnostics"), 0, true},
    };
    if (deep) {
        views.push_back({QStringLiteral("analysis_snapshot"), 32, true});
        views.push_back({QStringLiteral("live_latest"), 16, true});
        views.push_back({QStringLiteral("raw_ledger_tail"), 32, true});
        views.push_back({QStringLiteral("control_audit"), 32, true});
    }
    return views;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vsm-debug-tap"));
    QCoreApplication::setOrganizationName(QStringLiteral(CAN_MONITOR_ORG_NAME));
    QCoreApplication::setOrganizationDomain(QStringLiteral(CAN_MONITOR_ORG_DOMAIN));
    QCoreApplication::setApplicationVersion(QStringLiteral(CAN_MONITOR_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VSM passive-safe non-owning debug tap process"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption serverOption(QStringLiteral("server"),
                                          QStringLiteral("Core IPC server name to subscribe to."),
                                          QStringLiteral("name"));
    const QCommandLineOption outDirOption(QStringLiteral("out-dir"),
                                          QStringLiteral("Artifact directory for debug tap evidence."),
                                          QStringLiteral("dir"));
    const QCommandLineOption stopFileOption(QStringLiteral("stop-file"),
                                            QStringLiteral("Stop when this file appears."),
                                            QStringLiteral("path"));
    const QCommandLineOption deepOption(QStringLiteral("deep"),
                                        QStringLiteral("Also query bounded analysis/live/tail/control views."));
    parser.addOption(serverOption);
    parser.addOption(outDirOption);
    parser.addOption(stopFileOption);
    parser.addOption(deepOption);
    parser.process(app);

    const QString serverName = parser.value(serverOption).trimmed();
    const QString outDir = parser.value(outDirOption).trimmed().isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("debug_tap"))
        : parser.value(outDirOption).trimmed();
    DebugTapRuntime runtime(serverName,
                            outDir,
                            parser.value(stopFileOption).trimmed(),
                            defaultViews(parser.isSet(deepOption)));
    QString error;
    if (!runtime.start(&error)) {
        writeStdoutJson(QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-debug-tap")},
                                    {QStringLiteral("mode"), QStringLiteral("startup")},
                                    {QStringLiteral("ok"), false},
                                    {QStringLiteral("error"), error},
                                    {QStringLiteral("build"), buildInfoJson()}});
        return 2;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&runtime]() {
        runtime.stop(QStringLiteral("application_quit"));
    });
    return app.exec();
}
