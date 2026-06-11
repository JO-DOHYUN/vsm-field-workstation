#pragma once

#include "../CanTypes.h"
#include "../PacketParser.h"
#include "../Recorder.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CanMonitorTransport {

class LegacyIngressRuntime {
public:
    struct LoggingUpdate {
        bool ok = true;
        QString error;
        bool stateChanged = false;
        bool active = false;
        QString path;
        bool progressDue = false;
        quint64 bytesWritten = 0;
        quint64 frameCount = 0;
    };

    struct IngestResult {
        QVector<FrameRecordList> frameBatches;
        QVector<StatsRecord> stats;
        QStringList errors;
        bool loggingProgressDue = false;
        quint64 loggingBytesWritten = 0;
        quint64 loggingFrameCount = 0;
    };

    explicit LegacyIngressRuntime(QObject* recorderParent = nullptr);

    void resetStreamState();
    LoggingUpdate startLogging(const QString& binPath,
                               const QString& metaPath,
                               const QString& rulesSnapshotPath,
                               const QString& rulesSourcePath);
    LoggingUpdate stopLogging();
    LoggingUpdate stopLoggingIfActive();
    IngestResult ingest(const QByteArray& bytes);

private:
    LoggingUpdate makeLoggingUpdate(bool stateChanged, bool active, const QString& path, bool progressDue) const;
    bool loggingProgressDue();

    PacketParser m_parser;
    Recorder m_recorder;
    QElapsedTimer m_logProgressTimer;
    quint64 m_lastReportedFrameCount = 0;
};

} // namespace CanMonitorTransport
