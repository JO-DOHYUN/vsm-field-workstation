#pragma once

#include "core/CoreIpcServerRuntime.h"
#include "core/CoreMaterializedViewStore.h"
#include "transport/DrainByteQueue.h"
#include "transport/SerialDrainRuntime.h"
#include "transport/TypedEvidencePipelineWorkerRuntime.h"
#include "transport/TypedRecordHandoffQueue.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSharedPointer>
#include <QThread>

namespace CanMonitorCore {

class CaptureCoreProcessRuntime : public QObject {
    Q_OBJECT
public:
    explicit CaptureCoreProcessRuntime(QObject* parent = nullptr);
    ~CaptureCoreProcessRuntime() override;

    bool startIpc(const QString& serverName, QString* errorOut = nullptr);
    void stopIpc();
    bool isIpcListening() const;
    QString serverName() const;

    void startSerial(const QString& portName);
    void startGatewayTcp(const QString& endpoint);
    void stopTransport();
    QJsonObject statusJson() const;

signals:
    void ipcReady(const QString& serverName);
    void transportStateChanged(bool connected, const QString& message);
    void errorOccurred(const QString& message);

private:
    void ensureTransportRuntime();
    void teardownTransportRuntime();
    void seedInitialViews();
    void updateCoreHealth(const QString& state, CoreViewSeverity severity = CoreViewSeverity::Ok);
    void updateTransportSummary(const QJsonObject& payload,
                                CoreViewSeverity severity = CoreViewSeverity::Ok,
                                const QJsonObject& cheapCounts = {});
    void publishChange(const ViewChanged& change);
    void requestPipelineViewMirror(const QJsonObject& change);
    void applyPipelineSnapshot(quint64 requestId,
                               bool changed,
                               const QJsonObject& snapshot,
                               const QJsonObject& change);
    void handleHostFrameRequested(quint64 requestId, const QByteArray& frame, const QString& summary);
    void publishHostFrameWriteResult(bool ok, const QString& summary, quint64 bytesWritten);

    CoreMaterializedViewStore m_viewStore;
    CoreIpcServerRuntime m_ipc;
    QSharedPointer<CanMonitorTransport::DrainByteQueue> m_drainQueue;
    QSharedPointer<CanMonitorTransport::TypedRecordHandoffQueue> m_captureQueue;
    QThread m_drainThread;
    QThread m_pipelineThread;
    QPointer<CanMonitorTransport::SerialDrainRuntime> m_drainRuntime;
    QPointer<CanMonitorTransport::TypedEvidencePipelineWorkerRuntime> m_pipelineRuntime;
    QHash<quint64, CoreViewName> m_pendingMirrorRequests;
    QQueue<quint64> m_pendingHostFrameRequests;
    quint64 m_nextMirrorRequestId = 1;
    bool m_transportRuntimeStarted = false;
    bool m_transportConnected = false;
    QString m_transportMessage = QStringLiteral("idle");
};

} // namespace CanMonitorCore
