#include "backend/AppController.h"
#include "backend/AppLogging.h"
#include "backend/BuildMetadata.h"
#include "backend/CanTypes.h"
#include "backend/GraphViewportItem.h"
#include "backend/TypedRecords.h"

#include <QGuiApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <memory>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {
int hilControlPort(const QStringList& arguments) {
    for (int i = 1; i < arguments.size(); ++i) {
        const QString arg = arguments.at(i);
        if ((arg == QStringLiteral("--vsm-hil-control") || arg == QStringLiteral("--vsm-hil-control-port")) && i + 1 < arguments.size()) {
            return arguments.at(i + 1).toInt();
        }
        if (arg.startsWith(QStringLiteral("--vsm-hil-control="))) {
            return arg.mid(QStringLiteral("--vsm-hil-control=").size()).toInt();
        }
        if (arg.startsWith(QStringLiteral("--vsm-hil-control-port="))) {
            return arg.mid(QStringLiteral("--vsm-hil-control-port=").size()).toInt();
        }
    }
    return -1;
}

QJsonObject controllerStatus(const AppController& controller) {
    QJsonObject out;
    out.insert(QStringLiteral("connected"), controller.connected());
    out.insert(QStringLiteral("status_text"), controller.statusText());
    out.insert(QStringLiteral("transport_mode"), controller.transportMode());
    out.insert(QStringLiteral("board_alive"), controller.boardAlive());
    out.insert(QStringLiteral("typed_record_count"), QString::number(controller.typedRecordCount()));
    out.insert(QStringLiteral("typed_fault_count"), QString::number(controller.typedTransportFaultCount()));
    out.insert(QStringLiteral("typed_summary"), controller.typedEvidenceSummary());
    out.insert(QStringLiteral("typed_can_summary"), controller.typedCanSummary());
    out.insert(QStringLiteral("log_recording_active"), controller.logRecordingActive());
    out.insert(QStringLiteral("log_pending_save"), controller.logPendingSave());
    out.insert(QStringLiteral("log_stopping"), controller.logStopping());
    out.insert(QStringLiteral("log_saving"), controller.logSaving());
    out.insert(QStringLiteral("log_path"), controller.logPath());
    out.insert(QStringLiteral("log_bytes"), QString::number(controller.logRecordedBytes()));
    out.insert(QStringLiteral("log_records"), QString::number(controller.logRecordedFrameCount()));
    out.insert(QStringLiteral("transport_level"), controller.transportDiagnosticsLevel());
    out.insert(QStringLiteral("transport_summary"), controller.transportDiagnosticsSummary());
    out.insert(QStringLiteral("live_stats_summary"), controller.liveStatsSummary());
    out.insert(QStringLiteral("control_armed"), controller.controlArmed());
    out.insert(QStringLiteral("control_ready"), controller.controlReady());
    out.insert(QStringLiteral("control_verdict"), controller.controlActionVerdict());
    out.insert(QStringLiteral("control_summary"), controller.controlStatusSummary());
    out.insert(QStringLiteral("control_evidence"), controller.controlEvidenceStatsSummary());
    out.insert(QStringLiteral("control_last_command"), controller.controlLastCommandSummary());
    out.insert(QStringLiteral("control_last_write"), controller.controlLastWriteSummary());
    out.insert(QStringLiteral("control_last_ack"), controller.controlLastAckSummary());
    out.insert(QStringLiteral("control_last_audit"), controller.controlLastAuditSummary());
    return out;
}

void writeHilResponse(QTcpSocket* socket, const QJsonObject& response) {
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

std::unique_ptr<QTcpServer> installHilControlServer(AppController* controller, int port) {
    if (!controller || port <= 0) return {};
    auto server = std::make_unique<QTcpServer>();
    QObject::connect(server.get(), &QTcpServer::newConnection, server.get(), [controller, serverPtr = server.get()]() {
        while (QTcpSocket* socket = serverPtr->nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [controller, socket]() {
                while (socket->canReadLine()) {
                    const QByteArray line = socket->readLine().trimmed();
                    QJsonObject response;
                    response.insert(QStringLiteral("ok"), true);
                    if (line.isEmpty()) {
                        response.insert(QStringLiteral("status"), controllerStatus(*controller));
                        writeHilResponse(socket, response);
                        continue;
                    }

                    QJsonParseError parseError;
                    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
                    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                        response.insert(QStringLiteral("ok"), false);
                        response.insert(QStringLiteral("error"), QStringLiteral("invalid json: %1").arg(parseError.errorString()));
                        writeHilResponse(socket, response);
                        continue;
                    }

                    const QJsonObject request = doc.object();
                    const QString command = request.value(QStringLiteral("cmd")).toString().trimmed().toLower();
                    if (command == QStringLiteral("ping")) {
                        response.insert(QStringLiteral("pong"), true);
                    } else if (command == QStringLiteral("status")) {
                        response.insert(QStringLiteral("status"), controllerStatus(*controller));
                    } else if (command == QStringLiteral("connect")) {
                        const QString port = request.value(QStringLiteral("port")).toString(QStringLiteral("COM7"));
                        const QString mode = request.value(QStringLiteral("mode")).toString(QStringLiteral("typed"));
                        controller->setTransportMode(mode);
                        controller->connectPort(port);
                        response.insert(QStringLiteral("accepted"), QStringLiteral("connect"));
                    } else if (command == QStringLiteral("disconnect")) {
                        controller->disconnectPort();
                        response.insert(QStringLiteral("accepted"), QStringLiteral("disconnect"));
                    } else if (command == QStringLiteral("start_log")) {
                        const QString directory = request.value(QStringLiteral("directory")).toString();
                        const QString name = request.value(QStringLiteral("name")).toString();
                        if (!directory.isEmpty()) controller->setLogTargetDirectory(directory);
                        if (!name.isEmpty()) controller->setLogTargetName(name);
                        controller->startLog();
                        response.insert(QStringLiteral("accepted"), QStringLiteral("start_log"));
                    } else if (command == QStringLiteral("stop_log")) {
                        controller->stopLog();
                        response.insert(QStringLiteral("accepted"), QStringLiteral("stop_log"));
                    } else if (command == QStringLiteral("panel")) {
                        const QString key = request.value(QStringLiteral("key")).toString();
                        if (!key.isEmpty()) controller->setPanelActive(key, true);
                        response.insert(QStringLiteral("accepted"), QStringLiteral("panel"));
                    } else if (command == QStringLiteral("snapshot")) {
                        const QString path = request.value(QStringLiteral("path")).toString();
                        if (path.isEmpty()) {
                            response.insert(QStringLiteral("ok"), false);
                            response.insert(QStringLiteral("error"), QStringLiteral("snapshot path is required"));
                        } else {
                            controller->exportAnalysisSnapshot(path);
                            response.insert(QStringLiteral("accepted"), QStringLiteral("snapshot"));
                        }
                    } else if (command == QStringLiteral("control_arm")) {
                        controller->setControlArmed(request.value(QStringLiteral("armed")).toBool(true));
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_arm"));
                    } else if (command == QStringLiteral("control_target")) {
                        if (request.contains(QStringLiteral("bus"))) {
                            controller->setControlTargetBus(request.value(QStringLiteral("bus")).toInt(controller->controlTargetBus()));
                        }
                        if (request.contains(QStringLiteral("rpm"))) {
                            controller->setControlTargetRpm(request.value(QStringLiteral("rpm")).toInt(controller->controlTargetRpm()));
                        }
                        if (request.contains(QStringLiteral("steering"))) {
                            controller->setControlTargetSteeringDeg(request.value(QStringLiteral("steering")).toDouble(controller->controlTargetSteeringDeg()));
                        }
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_target"));
                    } else if (command == QStringLiteral("control_press")) {
                        controller->controlKeyboardPress(request.value(QStringLiteral("key")).toString());
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_press"));
                    } else if (command == QStringLiteral("control_release")) {
                        controller->controlKeyboardRelease(request.value(QStringLiteral("key")).toString());
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_release"));
                    } else if (command == QStringLiteral("control_release_all")) {
                        controller->controlKeyboardReleaseAll();
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_release_all"));
                    } else if (command == QStringLiteral("control_neutral")) {
                        controller->controlSendNeutral();
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_neutral"));
                    } else if (command == QStringLiteral("control_emergency_stop")) {
                        controller->controlEmergencyStop();
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_emergency_stop"));
                    } else if (command == QStringLiteral("control_manual")) {
                        controller->controlSendManual();
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_manual"));
                    } else if (command == QStringLiteral("control_pattern")) {
                        controller->controlRunPattern(request.value(QStringLiteral("pattern")).toString(QStringLiteral("sweep")));
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_pattern"));
                    } else if (command == QStringLiteral("control_stop_pattern")) {
                        controller->controlStopPattern();
                        response.insert(QStringLiteral("accepted"), QStringLiteral("control_stop_pattern"));
                    } else if (command == QStringLiteral("quit")) {
                        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
                        response.insert(QStringLiteral("accepted"), QStringLiteral("quit"));
                    } else {
                        response.insert(QStringLiteral("ok"), false);
                        response.insert(QStringLiteral("error"), QStringLiteral("unknown command"));
                    }
                    response.insert(QStringLiteral("status"), controllerStatus(*controller));
                    writeHilResponse(socket, response);
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        }
    });
    if (!server->listen(QHostAddress::LocalHost, quint16(port))) {
        qCCritical(logUi).noquote() << "HIL control listen failed" << port << server->errorString();
        return {};
    }
    qCInfo(logUi).noquote() << "HIL control listening 127.0.0.1:" << port;
    return server;
}
}

int main(int argc, char* argv[]) {
    qRegisterMetaType<FrameRecord>("FrameRecord");
    qRegisterMetaType<FrameRecordList>("FrameRecordList");
    qRegisterMetaType<StatsRecord>("StatsRecord");
    qRegisterMetaType<TypedRecord>("TypedRecord");
    qRegisterMetaType<TypedRecordList>("TypedRecordList");

#ifdef Q_OS_WIN
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif

    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Fusion"));
    QGuiApplication app(argc, argv);
    const BuildMetadata::Info buildInfo = BuildMetadata::current();
    app.setOrganizationName(buildInfo.organizationName);
    app.setOrganizationDomain(buildInfo.organizationDomain);
    app.setApplicationName(buildInfo.applicationName);

    AppLogging::initialize(buildInfo);
    qCInfo(logUi).noquote() << "Startup:" << BuildMetadata::banner(buildInfo);

    AppController controller;
    const int hilPort = hilControlPort(app.arguments());
    std::unique_ptr<QTcpServer> hilControlServer = installHilControlServer(&controller, hilPort);

    qmlRegisterType<GraphViewportItem>("CanMonitor", 1, 0, "GraphViewport");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appController", &controller);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            qCCritical(logUi) << "QML object creation failed";
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.loadFromModule("CanMonitor", "Main");

    const int exitCode = app.exec();
    qCInfo(logUi) << "Shutdown with exit code" << exitCode;
    AppLogging::shutdown();
#ifdef Q_OS_WIN
    timeEndPeriod(1);
#endif
    return exitCode;
}
