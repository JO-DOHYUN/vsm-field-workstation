#pragma once

#include "TypedRecords.h"

#include <QMap>
#include <QString>
#include <QVector>

class TypedReplayReader {
public:
    struct Fault {
        quint64 offset = 0;
        QString code;
        QString message;
    };

    struct RecordEntry {
        quint64 offset = 0;
        quint64 monoUs = 0;
        TypedRecord record;
    };

    struct Summary {
        QString inputPath;
        QString sessionDir;
        QString streamPath;
        QString metaPath;
        QString indexPath;
        QString eventsPath;
        quint64 bytesRead = 0;
        quint64 recordCount = 0;
        QMap<quint8, quint64> typeCounts;
        bool hasRecords = false;
        bool sessionContainer = false;
        bool streamFinal = false;
        bool streamPart = false;
        bool metaPresent = false;
        bool indexPresent = false;
        bool eventsPresent = false;
        bool metaPart = false;
        bool indexPart = false;
        bool eventsPart = false;
        bool partialCapture = false;
        bool indexSizeAligned = true;
        quint64 indexEntryCount = 0;
        quint64 indexByteCount = 0;
        quint64 indexRemainderBytes = 0;
        quint64 indexMismatchCount = 0;
        quint64 indexFirstOffset = 0;
        quint64 indexLastOffset = 0;
        quint64 indexFirstMonoUs = 0;
        quint64 indexLastMonoUs = 0;
        quint64 eventLineCount = 0;
        quint64 firstMonoUs = 0;
        quint64 lastMonoUs = 0;
        quint64 durationUs = 0;
        quint16 firstSeq = 0;
        quint16 lastSeq = 0;
        quint64 bytesDropped = 0;
        quint64 crcFailures = 0;
        quint64 lengthFailures = 0;
        quint64 versionWarnings = 0;
        quint64 seqGaps = 0;
        quint64 trailingBytes = 0;
        QString metaFormat;
        QString metaCreatedLocal;
        QString metaStreamFile;
        QString firstEventText;
        QString lastEventText;
        QString captureState;
        QString diagnosticSummary;
    };

    bool loadPath(const QString& path, QString* errorOut = nullptr);
    bool loadFile(const QString& path, QString* errorOut = nullptr);
    void reset();

    const QVector<RecordEntry>& records() const { return m_records; }
    const QVector<Fault>& faults() const { return m_faults; }
    const Summary& summary() const { return m_summary; }

private:
    static qsizetype findSof(const QByteArray& bytes, qsizetype from);
    static void setError(QString* errorOut, const QString& message);
    void addFault(quint64 offset, const QString& code, const QString& message);

    QVector<RecordEntry> m_records;
    QVector<Fault> m_faults;
    Summary m_summary;
};
