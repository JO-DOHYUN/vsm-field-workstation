#pragma once

#include "CanTypes.h"
#include "transport/RawLedgerRuntime.h"

#include <QAbstractListModel>
#include <QRegularExpression>
#include <QString>
#include <QVector>

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
    quint64 totalRows() const { return m_ledger.rowCount(); }
    QString idFilter() const { return m_idFilter; }
    int busFilter() const { return m_busFilter; }
    QString summary() const;
    QString sessionPath() const { return m_ledger.sessionPath(); }
    quint64 segmentBytes() const { return m_ledger.segmentBytes(); }
    quint64 droppedDisplayRows() const { return 0; }
    quint64 latestSeq() const { return m_latestSeq; }
    void setIdFilter(const QString& text);
    void setBusFilter(int bus);

    Q_INVOKABLE void clear();
    Q_INVOKABLE void resetFilters();

    void appendFrames(const FrameRecordList& frames);
    void appendTypedRecords(const TypedRecordList& records);

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

    static QVariant unreadableRowValue(int role, quint64 sourceRow);
    static QVector<IdFilterToken> parseIdFilterTokens(const QString& text);
    static bool parseTokenToId(const QString& token, quint32* out);
    static QString formatElapsedUs(quint64 us, quint64 baseUs);

    bool hasActiveFilter() const;
    bool rowMatches(quint64 sourceRow) const;
    void rebuildVisibleRows();
    quint64 sourceRowForDisplayRow(int row) const;

    CanMonitorTransport::RawLedgerRuntime m_ledger;
    QVector<quint64> m_visibleRows;
    QString m_idFilter;
    QVector<IdFilterToken> m_idFilterTokens;
    int m_busFilter = -1;
    quint64 m_firstTimeUs = 0;
    quint64 m_latestSeq = 0;
};
