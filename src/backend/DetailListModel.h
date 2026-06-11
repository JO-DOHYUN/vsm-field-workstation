#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QVariant>
#include <QString>

struct DetailRow {
    QString key;
    QString value;
    QString note;

    bool operator==(const DetailRow& other) const {
        return key == other.key && value == other.value && note == other.note;
    }
    bool operator!=(const DetailRow& other) const { return !(*this == other); }
};

class DetailListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        ValueRole,
        NoteRole
    };

    explicit DetailListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }

    Q_INVOKABLE void clear();
    void setRows(const QVector<DetailRow>& rows);

signals:
    void countChanged();

private:
    QVector<DetailRow> m_rows;
};
