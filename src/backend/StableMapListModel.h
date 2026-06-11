#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>
#include <QVector>

class StableMapListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    explicit StableMapListModel(const QStringList& roles = {}, const QString& keyField = QStringLiteral("key"), QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int count() const { return rowCount(); }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRoles(const QStringList& roles);
    void setKeyField(const QString& keyField);
    void setRows(const QVector<QVariantMap>& rows);
    void setRowsRelaxed(const QVector<QVariantMap>& rows, bool allowStructuralChanges);
    void clear();

    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE int indexOfKey(const QString& key) const;
    Q_INVOKABLE int findIndex(const QString& field, const QVariant& value) const;

    QStringList orderedKeys() const;

signals:
    void countChanged();
    void rowsChanged();
    void structureChanged();

private:
    void emitContiguousDataChanged(const QVector<int>& rows);

    bool sameKeyOrder(const QVector<QVariantMap>& rows) const;
    QString rowKey(const QVariantMap& row) const;

    QVector<QVariantMap> m_rows;
    QStringList m_roleNamesList;
    QString m_keyField;
    QHash<int, QByteArray> m_roles;
};
