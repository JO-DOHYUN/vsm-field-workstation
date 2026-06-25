#pragma once

#include "CanTypes.h"
#include "TypedRecords.h"

#include <QAbstractListModel>
#include <QRegularExpression>
#include <QString>
#include <QVector>

#include <deque>

class RawFrameTableModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(qulonglong totalRows READ totalRows NOTIFY totalRowsChanged)
    Q_PROPERTY(QString idFilter READ idFilter WRITE setIdFilter NOTIFY filtersChanged)
    Q_PROPERTY(int busFilter READ busFilter WRITE setBusFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong segmentBytes READ segmentBytes NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong droppedDisplayRows READ droppedDisplayRows NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong latestSeq READ latestSeq NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong ledgerReadFailCount READ ledgerReadFailCount NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong ledgerFileReadFailCount READ ledgerFileReadFailCount NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong ledgerCacheHits READ ledgerCacheHits NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong ledgerCacheMisses READ ledgerCacheMisses NOTIFY summaryChanged)
    Q_PROPERTY(int ledgerCacheRows READ ledgerCacheRows NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong writerQueueBytes READ writerQueueBytes NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong writerMaxQueueBytes READ writerMaxQueueBytes NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong writerOverrunBytes READ writerOverrunBytes NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong writerMaxUs READ writerMaxUs NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong writerFailures READ writerFailures NOTIFY summaryChanged)

public:
    enum Roles {
        LedgerSeqRole = Qt::UserRole + 1,
        RowNumberRole,
        IdRole,
        IdTextRole,
        BusRole,
        DlcRole,
        DataHexRole,
        TimeUsRole,
        TimeTextRole,
        FlagsRole,
        SourceRole,
        ValidRole
    };

    explicit RawFrameTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }
    quint64 totalRows() const { return m_totalRows; }
    QString idFilter() const { return m_idFilter; }
    int busFilter() const { return m_busFilter; }
    QString summary() const;
    QString sessionPath() const { return m_sessionPath; }
    quint64 segmentBytes() const { return m_segmentBytes; }
    quint64 droppedDisplayRows() const { return m_totalRows > quint64(m_tailRows.size()) ? m_totalRows - quint64(m_tailRows.size()) : 0; }
    quint64 latestSeq() const { return m_latestSeq; }
    quint64 ledgerReadFailCount() const { return 0; }
    quint64 ledgerFileReadFailCount() const { return m_writerFailures; }
    quint64 ledgerCacheHits() const { return 0; }
    quint64 ledgerCacheMisses() const { return 0; }
    int ledgerCacheRows() const { return int(m_tailRows.size()); }
    quint64 writerQueueBytes() const { return m_writerQueueBytes; }
    quint64 writerMaxQueueBytes() const { return m_writerMaxQueueBytes; }
    quint64 writerOverrunBytes() const { return m_writerOverrunBytes; }
    quint64 writerMaxUs() const { return m_writerMaxUs; }
    quint64 writerFailures() const { return m_writerFailures; }
    void setIdFilter(const QString& text);
    void setBusFilter(int bus);

    Q_INVOKABLE void clear();
    Q_INVOKABLE void resetFilters();

    void appendFrames(const FrameRecordList& frames);
    void appendTypedRecords(const TypedRecordList& records);
    void resetLedgerState(const QString& sessionPath = QString(), const QString& error = QString());
    void applyCommittedFrames(const FrameRecordList& frames,
                              quint64 firstSeq,
                              quint64 lastSeq,
                              quint64 totalRows,
                              quint64 segmentBytes,
                              const QString& sessionPath);
    void updateWriterStatus(quint64 queueBytes,
                            quint64 maxQueueBytes,
                            quint64 overrunBytes,
                            quint64 writeMaxUs,
                            quint64 writeFailures,
                            const QString& lastError);

signals:
    void countChanged();
    void totalRowsChanged();
    void filtersChanged();
    void summaryChanged();
    void rowsAppended(qulonglong firstSeq, qulonglong lastSeq, int count);

private:
    friend class RawLedgerRuntimeTest;

    struct IdFilterToken {
        QString textUpper;
        bool hasId = false;
        quint32 id = 0;
    };

    struct DisplayRow {
        quint64 ledgerSeq = 0;
        FrameRecord frame;
    };

    static QVariant unreadableRowValue(int role, quint64 sourceRow);
    static QVector<IdFilterToken> parseIdFilterTokens(const QString& text);
    static bool parseTokenToId(const QString& token, quint32* out);
    static QString formatElapsedUs(quint64 us, quint64 baseUs);

    bool hasActiveFilter() const;
    bool rowMatches(const DisplayRow& row) const;
    void rebuildVisibleRows();
    const DisplayRow* displayRow(int row) const;
    void appendTailRows(const FrameRecordList& frames, quint64 firstSeq);

    static constexpr int kDisplayTailLimit = 30000;
    std::deque<DisplayRow> m_tailRows;
    QVector<quint64> m_visibleRows;
    QString m_idFilter;
    QVector<IdFilterToken> m_idFilterTokens;
    int m_busFilter = -1;
    QString m_sessionPath;
    QString m_writerLastError;
    quint64 m_totalRows = 0;
    quint64 m_segmentBytes = 0;
    quint64 m_firstTimeUs = 0;
    quint64 m_latestSeq = 0;
    quint64 m_writerQueueBytes = 0;
    quint64 m_writerMaxQueueBytes = 0;
    quint64 m_writerOverrunBytes = 0;
    quint64 m_writerMaxUs = 0;
    quint64 m_writerFailures = 0;
};
