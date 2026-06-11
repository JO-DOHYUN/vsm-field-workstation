#pragma once

#include "HostTxQueue.h"

#include <optional>

namespace CanMonitorTransport {

class HostTxRuntime {
public:
    struct Status {
        quint64 queuedFrames = 0;
        quint64 queuedBytes = 0;
        quint64 enqueuedFrames = 0;
        quint64 writtenFrames = 0;
        quint64 droppedFrames = 0;
    };

    struct EnqueueResult {
        bool ok = true;
        QString error;
        Status status;
    };

    struct WriteItem {
        QByteArray frame;
        QString summary;
    };

    struct ClearResult {
        bool hadPending = false;
        QString error;
        Status status;
    };

    explicit HostTxRuntime(qsizetype maxInFlightBytes = 8192);

    EnqueueResult enqueue(const QByteArray& frame, const QString& summary);
    std::optional<WriteItem> takeNextForWrite(qsizetype serialBytesToWrite);
    void markWritten();
    ClearResult clear(const QString& reason);
    Status status() const;

private:
    HostTxQueue m_queue;
    qsizetype m_maxInFlightBytes = 8192;
};

} // namespace CanMonitorTransport
