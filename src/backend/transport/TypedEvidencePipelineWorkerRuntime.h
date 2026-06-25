#pragma once

#include "transport/DrainByteQueue.h"
#include "transport/TypedEvidencePipelineRuntime.h"

#include <QObject>
#include <QSharedPointer>
#include <QStringList>

namespace CanMonitorTransport {

class TypedEvidencePipelineWorkerRuntime : public QObject {
    Q_OBJECT
public:
    explicit TypedEvidencePipelineWorkerRuntime(QSharedPointer<DrainByteQueue> queue,
                                                QObject* parent = nullptr);

public slots:
    void reset();
    void schedulePump(qint64 handshakeElapsedMs);

signals:
    void capabilityFirstSeen(qint64 elapsedMs, quint64 bytes);
    void errorsOccurred(const QStringList& errors);
    void recordBatchReady(const TypedRecordList& records);
    void typedStatusReady(quint64 frames,
                          quint64 bytesDropped,
                          quint64 crcFailures,
                          quint64 lengthFailures,
                          quint64 versionWarnings,
                          quint64 seqGaps);
    void pipelineStatusChanged(quint64 parseBacklogBytes, quint64 parserBatchMaxMs);
    void captureDiagnosticsChanged(const QJsonObject& diagnostics);

private slots:
    void pump();

private:
    QSharedPointer<DrainByteQueue> m_queue;
    TypedEvidencePipelineRuntime m_pipeline;
    bool m_pumpScheduled = false;
    qint64 m_handshakeElapsedMs = -1;
};

} // namespace CanMonitorTransport
