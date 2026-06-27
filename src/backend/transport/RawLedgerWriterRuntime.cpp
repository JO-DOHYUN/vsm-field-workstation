#include "transport/RawLedgerWriterRuntime.h"

#include <algorithm>

namespace CanMonitorTransport {

RawLedgerWriterRuntime::RawLedgerWriterRuntime(QObject* parent)
    : QObject(parent) {}

void RawLedgerWriterRuntime::reset(const QString& label) {
    m_batchCount = 0;
    m_writeMaxUs = 0;
    m_writeFailures = 0;
    m_lastError.clear();
    const bool ok = m_ledger.reset(label);
    if (!ok) {
        ++m_writeFailures;
        m_lastError = m_ledger.lastError();
    }
    emit resetCompleted(ok, m_ledger.sessionPath(), m_lastError);
    emitStatus();
}

void RawLedgerWriterRuntime::appendFrames(FrameRecordList frames) {
    QElapsedTimer timer;
    timer.start();
    const auto result = m_ledger.appendFrames(frames);
    const quint64 elapsedUs = quint64(timer.nsecsElapsed() / 1000);
    m_writeMaxUs = std::max(m_writeMaxUs, elapsedUs);
    ++m_batchCount;

    if (!result.ok) {
        ++m_writeFailures;
        m_lastError = result.error;
    } else if (result.appended > 0) {
        emit batchCommitted(result.committedFrames,
                            result.firstSeq,
                            result.lastSeq,
                            result.totalRows,
                            result.segmentBytes,
                            m_ledger.sessionPath());
    }
    emitStatus();
    emit batchFinished();
}

void RawLedgerWriterRuntime::emitStatus() {
    emit statusChanged(m_ledger.rowCount(),
                       m_ledger.segmentBytes(),
                       m_batchCount,
                       m_writeMaxUs,
                       m_writeFailures,
                       m_lastError);
}

} // namespace CanMonitorTransport
