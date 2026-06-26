#include "backend/BuildMetadata.h"
#include "backend/core/CoreMaterializedViewStore.h"
#include "backend/core/CoreIpcServerRuntime.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace {

void writeJsonLine(const QJsonObject& object) {
    QTextStream stream(stdout);
    stream << QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)) << Qt::endl;
}

QJsonObject buildInfoJson() {
    return QJsonObject::fromVariantMap(BuildMetadata::toVariantMap(BuildMetadata::current()));
}

int runSelfTest() {
    CanMonitorCore::CoreMaterializedViewStore store;
    QJsonArray frames;
    frames.append(QJsonObject{{QStringLiteral("bus"), 0},
                              {QStringLiteral("can_id"), 0x123},
                              {QStringLiteral("dlc"), 8}});
    const auto change = store.updateArrayView(CanMonitorCore::CoreViewName::LiveLatest,
                                              QStringLiteral("frames"),
                                              frames,
                                              CanMonitorCore::CoreViewSeverity::Ok,
                                              QJsonObject{{QStringLiteral("key_count"), 1}});
    const auto result = store.queryView({CanMonitorCore::CoreViewName::LiveLatest, 0, 1});
    const bool ok = result.changed
        && change.viewSeq == result.snapshot.viewSeq
        && result.snapshot.payload.value(QStringLiteral("frames")).toArray().size() == 1;

    writeJsonLine(QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-capture-core")},
                              {QStringLiteral("mode"), QStringLiteral("self_test")},
                              {QStringLiteral("ok"), ok},
                              {QStringLiteral("build"), buildInfoJson()},
                              {QStringLiteral("view_seq"), QString::number(result.snapshot.viewSeq)}});
    return ok ? 0 : 2;
}

void seedInitialViews(CanMonitorCore::CoreMaterializedViewStore& store) {
    store.updateView(CanMonitorCore::CoreViewName::CoreHealth,
                     QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-capture-core")},
                                 {QStringLiteral("state"), QStringLiteral("ready")},
                                 {QStringLiteral("build"), buildInfoJson()}},
                     CanMonitorCore::CoreViewSeverity::Ok,
                     QJsonObject{{QStringLiteral("state"), QStringLiteral("ready")}});
    store.updateView(CanMonitorCore::CoreViewName::TransportSummary,
                     QJsonObject{{QStringLiteral("transport"), QStringLiteral("idle")},
                                 {QStringLiteral("serial_owner"), QStringLiteral("core")},
                                 {QStringLiteral("capture_active"), false}},
                     CanMonitorCore::CoreViewSeverity::Ok,
                     QJsonObject{{QStringLiteral("transport"), QStringLiteral("idle")}});
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vsm-capture-core"));
    QCoreApplication::setOrganizationName(QStringLiteral(CAN_MONITOR_ORG_NAME));
    QCoreApplication::setOrganizationDomain(QStringLiteral(CAN_MONITOR_ORG_DOMAIN));
    QCoreApplication::setApplicationVersion(QStringLiteral(CAN_MONITOR_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VSM capture core data-plane process"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption selfTestOption(QStringLiteral("self-test"),
                                            QStringLiteral("Run core view-store self test and exit."));
    const QCommandLineOption readyOption(QStringLiteral("ready-json"),
                                         QStringLiteral("Print a ready JSON object and exit."));
    const QCommandLineOption serverOption(QStringLiteral("server"),
                                          QStringLiteral("Run the local IPC server with the given name."),
                                          QStringLiteral("name"));
    parser.addOption(selfTestOption);
    parser.addOption(readyOption);
    parser.addOption(serverOption);
    parser.process(app);

    if (parser.isSet(selfTestOption)) {
        return runSelfTest();
    }
    if (parser.isSet(readyOption)) {
        writeJsonLine(QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-capture-core")},
                                  {QStringLiteral("mode"), QStringLiteral("ready")},
                                  {QStringLiteral("ok"), true},
                                  {QStringLiteral("build"), buildInfoJson()}});
        return 0;
    }
    if (parser.isSet(serverOption)) {
        CanMonitorCore::CoreMaterializedViewStore store;
        seedInitialViews(store);
        CanMonitorCore::CoreIpcServerRuntime server(&store);
        QString error;
        const QString serverName = parser.value(serverOption);
        if (!server.listen(serverName, &error)) {
            writeJsonLine(QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-capture-core")},
                                      {QStringLiteral("mode"), QStringLiteral("server")},
                                      {QStringLiteral("ok"), false},
                                      {QStringLiteral("error"), error}});
            return 3;
        }
        writeJsonLine(QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-capture-core")},
                                  {QStringLiteral("mode"), QStringLiteral("server")},
                                  {QStringLiteral("ok"), true},
                                  {QStringLiteral("server_name"), serverName},
                                  {QStringLiteral("build"), buildInfoJson()}});
        return app.exec();
    }

    writeJsonLine(QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-capture-core")},
                              {QStringLiteral("mode"), QStringLiteral("idle")},
                              {QStringLiteral("ok"), true},
                              {QStringLiteral("note"), QStringLiteral("IPC/serial ownership is the next migration slice.")},
                              {QStringLiteral("build"), buildInfoJson()}});
    return 0;
}
