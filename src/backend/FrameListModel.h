#pragma once

#include "CanTypes.h"
#include <QAbstractListModel>
#include <QStringList>
#include <deque>

class FrameListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        IdTextRole,
        BusRole,
        DataHexRole,
        TimeUsRole,
        TimeTextRole,
        SourceRole,
        DlcRole
    };

    explicit FrameListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }

    Q_INVOKABLE void clear();
    void appendLive(const FrameRecord& fr, const QString& timeText = QString());
    void appendReplay(const FrameRecord& fr, const QString& timeText = QString());
    void appendLiveBatch(const FrameRecordList& frames, const QStringList& timeTexts = {});
    void appendReplayBatch(const FrameRecordList& frames, const QStringList& timeTexts = {});

signals:
    void countChanged();

private:
    struct Row {
        FrameRecord fr;
        QString source;
        QString timeText;
        QString dataHex;
        int dlc = 0;
    };

    void append(const FrameRecord& fr, const QString& source, const QString& timeText);
    void appendBatch(const FrameRecordList& frames, const QString& source, const QStringList& timeTexts);

    QString formatRelativeTime(quint64 us) const;

    std::deque<Row> m_rows;
    int m_limit = 700;
    quint64 m_firstTimeUs = 0;
};
