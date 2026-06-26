#include "backend/BuildMetadata.h"
#include "backend/core/CaptureCoreProcessRuntime.h"
#include "backend/core/CoreMaterializedViewStore.h"

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
    const QCommandLineOption portOption(QStringLiteral("port"),
                                        QStringLiteral("Open the serial port from the capture core process."),
                                        QStringLiteral("port"));
    const QCommandLineOption gatewayOption(QStringLiteral("gateway"),
                                           QStringLiteral("Open a debug gateway TCP endpoint from the capture core process."),
                                           QStringLiteral("endpoint"));
    parser.addOption(selfTestOption);
    parser.addOption(readyOption);
    parser.addOption(serverOption);
    parser.addOption(portOption);
    parser.addOption(gatewayOption);
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
        CanMonitorCore::CaptureCoreProcessRuntime runtime;
        QString error;
        const QString serverName = parser.value(serverOption);
        if (!runtime.startIpc(serverName, &error)) {
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
        if (parser.isSet(portOption)) {
            runtime.startSerial(parser.value(portOption));
        } else if (parser.isSet(gatewayOption)) {
            runtime.startGatewayTcp(parser.value(gatewayOption));
        }
        return app.exec();
    }

    writeJsonLine(QJsonObject{{QStringLiteral("process"), QStringLiteral("vsm-capture-core")},
                              {QStringLiteral("mode"), QStringLiteral("idle")},
                              {QStringLiteral("ok"), true},
                              {QStringLiteral("note"), QStringLiteral("Use --server <name> with optional --port or --gateway to run the core data-plane owner.")},
                              {QStringLiteral("build"), buildInfoJson()}});
    return 0;
}
