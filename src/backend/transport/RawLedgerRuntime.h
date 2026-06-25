#pragma once

#include "../CanTypes.h"
#include "../TypedRecords.h"

#include <QFile>
#include <QHash>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QVector>

#include <optional>

namespace CanMonitorTransport {

class RawLedgerRuntime {
public:
    struct Row {
        quint64 ledgerSeq = 0;
        quint64 tExtUs = 0;
        quint32 canId = 0;
        quint8 bus = 0;
        quint8 dlc = 0;
        bool ext = false;
        bool rtr = false;
        quint8 data[8] = {0};
        quint8 sourceSeq = 0;
        bool hasCaptureSeq = false;
        quint64 captureSeq = 0;
    };

    struct AppendResult {
        bool ok = false;
        quint64 firstSeq = 0;
        quint64 lastSeq = 0;
        int appended = 0;
        quint64 totalRows = 0;
        quint64 segmentBytes = 0;
        FrameRecordList committedFrames;
        QString error;
    };

    struct Diagnostics {
        quint64 committedRows = 0;
        quint64 blocks = 0;
        quint64 segmentBytes = 0;
        int cacheRows = 0;
        quint64 cacheHits = 0;
        quint64 cacheMisses = 0;
        quint64 readFailCount = 0;
        quint64 fileReadFailCount = 0;
    };

    RawLedgerRuntime();
    ~RawLedgerRuntime();

    bool reset(const QString& label = QString());
    AppendResult appendFrames(const FrameRecordList& frames);
    AppendResult appendTypedRecords(const TypedRecordList& records);
    std::optional<Row> readRow(quint64 row) const;
    Diagnostics diagnostics() const;

    quint64 rowCount() const { return m_rowCount; }
    quint64 committedRowCount() const { return m_rowCount; }
    quint64 segmentBytes() const { return m_segmentBytes; }
    QString sessionPath() const { return m_segmentPath; }
    bool isOpen() const { return m_file.isOpen(); }
    QString lastError() const { return m_lastError; }

private:
    struct BlockRef {
        quint64 firstRow = 0;
        quint32 frameCount = 0;
        quint64 blockOffset = 0;
        quint16 typedSeq = 0;
    };

    static QByteArray makeSegmentPayloadFromFrame(const FrameRecord& frame);
    static QByteArray makeSegmentPayloadFromCanRaw(const TypedRecord& record, const TypedCanRawRecord& can);
    static QByteArray encodeBlock(const QByteArray& segmentPayload, quint16 typedSeq);
    static std::optional<Row> decodeRowFromPayload(const QByteArray& segmentPayload, quint16 typedSeq, quint16 frameIndex);
    AppendResult appendSegmentPayloads(const QVector<QPair<QByteArray, quint16>>& payloads);
    static FrameRecord frameFromRow(const Row& row);
    void cacheRow(quint64 row, const Row& value) const;
    std::optional<Row> noteReadFailure(bool fileAttempted) const;

    QScopedPointer<QTemporaryDir> m_tempDir;
    mutable QFile m_file;
    mutable QFile m_indexFile;
    mutable QHash<quint64, Row> m_cache;
    mutable QVector<quint64> m_cacheOrderRing;
    mutable int m_cacheOrderHead = 0;
    mutable int m_cacheOrderSize = 0;
    QString m_segmentPath;
    QString m_indexPath;
    QString m_lastError;
    quint64 m_nextSeq = 0;
    quint64 m_rowCount = 0;
    quint64 m_blockCount = 0;
    quint64 m_segmentBytes = 0;
    mutable quint64 m_cacheHits = 0;
    mutable quint64 m_cacheMisses = 0;
    mutable quint64 m_readFailCount = 0;
    mutable quint64 m_fileReadFailCount = 0;
};

} // namespace CanMonitorTransport
