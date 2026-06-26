#pragma once

#include "core/CoreIpcClientRuntime.h"
#include "core/CoreViewClientRuntime.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

namespace CanMonitorCore {

class CoreProcessClientRuntime : public QObject {
    Q_OBJECT
public:
    explicit CoreProcessClientRuntime(QObject* parent = nullptr);
    ~CoreProcessClientRuntime() override;

    bool startSerial(const QString& executablePath, const QString& portName, QString* errorOut = nullptr);
    bool startGatewayTcp(const QString& executablePath, const QString& endpoint, QString* errorOut = nullptr);
    bool startServerOnly(const QString& executablePath, QString* errorOut = nullptr);
    void stop();
    bool isActive() const;
    bool isIpcConnected() const;
    QString serverName() const { return m_serverName; }
    QJsonObject statusJson() const;

    bool requestView(const CoreViewClientRuntime::ViewRequest& request);
    bool sendHostFrame(const QByteArray& frame, const QString& summary, QString* errorOut = nullptr);

signals:
    void stateChanged(bool active, const QString& message);
    void viewChanged(const QJsonObject& change);
    void viewSnapshotReady(quint64 requestId, bool changed, const QJsonObject& snapshot, const QJsonObject& change);
    void hostFrameWriteResult(bool ok, const QString& summary, quint64 bytesWritten);
    void errorOccurred(const QString& message);

private:
    bool startProcess(const QString& executablePath, const QStringList& extraArgs, QString* errorOut);
    void connectClientSignals();
    void readStandardOutput();
    void readStandardError();
    void connectIpc();
    static QString makeServerName();

    QProcess m_process;
    CoreIpcClientRuntime m_client;
    QByteArray m_stdoutBuffer;
    QString m_serverName;
    QString m_lastMessage = QStringLiteral("core process idle");
    bool m_startupSeen = false;
};

} // namespace CanMonitorCore
