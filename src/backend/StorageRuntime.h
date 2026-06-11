#pragma once

#include "TypedRecords.h"

#include <QFile>
#include <QJsonObject>
#include <QString>

class StorageRuntime {
public:
    struct Paths {
        QString sessionDir;
        QString streamPart;
        QString streamFinal;
        QString indexPart;
        QString indexFinal;
        QString metaPart;
        QString metaFinal;
        QString eventsPart;
        QString eventsFinal;
    };

    struct LogSessionPaths {
        bool typedSession = false;
        QString recordPath;
        QString metaPath;
        QString modelPath;
        QString suggestedSavePath;
    };

    struct PendingSavePaths {
        bool valid = false;
        QString finalBin;
        QString finalMeta;
        QString finalModel;
    };

    static QString defaultLogDirectory();
    static QString defaultSnapshotDirectory();
    static QString defaultTempLogDirectory();
    static LogSessionPaths makeTypedCapturePaths(const QString& stamp);
    static LogSessionPaths makeTypedCapturePaths(const QString& stamp, const QString& targetDirectory, const QString& targetName);
    static LogSessionPaths makeLegacyLogPaths(const QString& stamp, bool includeModelSnapshot);
    static LogSessionPaths makeLegacyLogPaths(const QString& stamp, bool includeModelSnapshot, const QString& targetDirectory, const QString& targetName);
    static PendingSavePaths makePendingSavePaths(const QString& requestedPath);

    bool startTypedSession(const QString& sessionDir, const QJsonObject& metadata, QString* errorOut = nullptr);
    bool appendTypedRecord(const TypedRecord& record, QString* errorOut = nullptr);
    bool appendEventJsonLine(const QJsonObject& event, QString* errorOut = nullptr);
    bool finalizeTypedSession(QString* errorOut = nullptr);
    void discard();

    bool isActive() const { return m_active; }
    const Paths& paths() const { return m_paths; }
    quint64 recordCount() const { return m_recordCount; }
    quint64 bytesWritten() const { return m_bytesWritten; }

private:
    static Paths makePaths(const QString& sessionDir);
    static bool replacePartFile(const QString& partPath, const QString& finalPath, QString* errorOut);
    static void setError(QString* errorOut, const QString& message);
    void closeFiles();

    QFile m_stream;
    QFile m_index;
    QFile m_events;
    Paths m_paths;
    bool m_active = false;
    quint64 m_recordCount = 0;
    quint64 m_bytesWritten = 0;
};
