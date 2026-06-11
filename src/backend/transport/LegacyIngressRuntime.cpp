#include "transport/LegacyIngressRuntime.h"

namespace {
constexpr int kLegacyLiveEmitBatchSize = 192;
constexpr quint64 kLegacyLoggingProgressFrameStep = 1536;
constexpr int kLegacyLoggingProgressIntervalMs = 1000;
}

namespace CanMonitorTransport {

LegacyIngressRuntime::LegacyIngressRuntime(QObject* recorderParent)
    : m_recorder(recorderParent) {}

void LegacyIngressRuntime::resetStreamState() {
    m_parser.reset();
}

LegacyIngressRuntime::LoggingUpdate LegacyIngressRuntime::startLogging(const QString& binPath,
                                                                       const QString& metaPath,
                                                                       const QString& rulesSnapshotPath,
                                                                       const QString& rulesSourcePath) {
    QString error;
    if (!m_recorder.start(binPath, metaPath, rulesSnapshotPath, rulesSourcePath, &error)) {
        LoggingUpdate update;
        update.ok = false;
        update.error = QStringLiteral("로그 시작 실패: %1").arg(error);
        return update;
    }

    m_lastReportedFrameCount = 0;
    m_logProgressTimer.restart();
    return makeLoggingUpdate(true, true, m_recorder.currentPath(), true);
}

LegacyIngressRuntime::LoggingUpdate LegacyIngressRuntime::stopLogging() {
    if (!m_recorder.isActive()) return {};

    const QString path = m_recorder.currentPath();
    m_recorder.stop();
    LoggingUpdate update = makeLoggingUpdate(true, false, path, true);
    const QString warning = m_recorder.lastError().trimmed();
    if (!warning.isEmpty()) {
        update.error = QStringLiteral("로그 종료 후처리 경고: %1").arg(warning);
    }
    return update;
}

LegacyIngressRuntime::LoggingUpdate LegacyIngressRuntime::stopLoggingIfActive() {
    return m_recorder.isActive() ? stopLogging() : LoggingUpdate{};
}

LegacyIngressRuntime::IngestResult LegacyIngressRuntime::ingest(const QByteArray& bytes) {
    IngestResult result;
    if (bytes.isEmpty()) return result;

    m_parser.append(bytes);

    FrameRecordList batch;
    batch.reserve(kLegacyLiveEmitBatchSize);
    auto flushBatch = [&result, &batch]() {
        if (batch.isEmpty()) return;
        result.frameBatches.push_back(batch);
        batch.clear();
        batch.reserve(kLegacyLiveEmitBatchSize);
    };

    while (true) {
        auto record = m_parser.takeOne();
        if (!record) break;
        if (record->isStats) {
            flushBatch();
            if (m_recorder.isActive()) m_recorder.flushPending(false);
            result.stats.push_back(record->stats);
        } else {
            batch.push_back(record->frame);
            if (m_recorder.isActive()) {
                m_recorder.append20(record->frame.raw20);
            }
            if (batch.size() >= kLegacyLiveEmitBatchSize) flushBatch();
        }
    }

    flushBatch();
    if (m_recorder.isActive()) {
        m_recorder.flushPending(false);
        if (loggingProgressDue()) {
            result.loggingProgressDue = true;
            result.loggingBytesWritten = m_recorder.bytesWritten();
            result.loggingFrameCount = m_recorder.frameCount();
        }
    }

    return result;
}

LegacyIngressRuntime::LoggingUpdate LegacyIngressRuntime::makeLoggingUpdate(bool stateChanged,
                                                                            bool active,
                                                                            const QString& path,
                                                                            bool progressDue) const {
    LoggingUpdate update;
    update.stateChanged = stateChanged;
    update.active = active;
    update.path = path;
    update.progressDue = progressDue;
    update.bytesWritten = m_recorder.bytesWritten();
    update.frameCount = m_recorder.frameCount();
    return update;
}

bool LegacyIngressRuntime::loggingProgressDue() {
    const quint64 frames = m_recorder.frameCount();
    const bool byCount = frames >= m_lastReportedFrameCount + kLegacyLoggingProgressFrameStep;
    const bool byTime = !m_logProgressTimer.isValid() || m_logProgressTimer.elapsed() >= kLegacyLoggingProgressIntervalMs;
    if (!byCount && !byTime) return false;
    m_lastReportedFrameCount = frames;
    m_logProgressTimer.restart();
    return true;
}

} // namespace CanMonitorTransport
