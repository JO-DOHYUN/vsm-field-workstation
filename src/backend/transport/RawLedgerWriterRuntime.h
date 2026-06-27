#pragma once

#include "transport/RawLedgerRuntime.h"

#include <QElapsedTimer>
#include <QObject>

namespace CanMonitorTransport {

class RawLedgerWriterRuntime : public QObject {
    Q_OBJECT
public:
    explicit RawLedgerWriterRuntime(QObject* parent = nullptr);

public slots:
    void reset(const QString& label);
    void appendFrames(FrameRecordList frames);

signals:
    void resetCompleted(bool ok, const QString& path, const QString& error);
    void batchCommitted(const FrameRecordList& frames,
                        quint64 firstSeq,
                        quint64 lastSeq,
                        quint64 totalRows,
                        quint64 segmentBytes,
                        const QString& path);
    void statusChanged(quint64 totalRows,
                       quint64 segmentBytes,
                       quint64 batchCount,
                       quint64 writeMaxUs,
                       quint64 writeFailures,
                       const QString& lastError);
    void batchFinished();

private:
    void emitStatus();

    RawLedgerRuntime m_ledger;
    quint64 m_batchCount = 0;
    quint64 m_writeMaxUs = 0;
    quint64 m_writeFailures = 0;
    QString m_lastError;
};

} // namespace CanMonitorTransport
