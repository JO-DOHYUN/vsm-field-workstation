#include "StableMapListModel.h"

#include <QSet>

StableMapListModel::StableMapListModel(const QStringList& roles, const QString& keyField, QObject* parent)
    : QAbstractListModel(parent), m_keyField(keyField) {
    setRoles(roles);
}

int StableMapListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_rows.size();
}

QVariant StableMapListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const auto it = m_roles.constFind(role);
    if (it == m_roles.cend()) return {};
    return m_rows.at(index.row()).value(QString::fromUtf8(it.value()));
}

QHash<int, QByteArray> StableMapListModel::roleNames() const {
    return m_roles;
}

void StableMapListModel::setRoles(const QStringList& roles) {
    m_roleNamesList = roles;
    m_roles.clear();
    int role = Qt::UserRole;
    for (const QString& name : m_roleNamesList) {
        m_roles.insert(role++, name.toUtf8());
    }
}

void StableMapListModel::setKeyField(const QString& keyField) {
    m_keyField = keyField;
}

QString StableMapListModel::rowKey(const QVariantMap& row) const {
    return row.value(m_keyField).toString();
}

bool StableMapListModel::sameKeyOrder(const QVector<QVariantMap>& rows) const {
    if (rows.size() != m_rows.size()) return false;
    for (int i = 0; i < rows.size(); ++i) {
        if (rowKey(rows.at(i)) != rowKey(m_rows.at(i))) return false;
    }
    return true;
}

void StableMapListModel::setRows(const QVector<QVariantMap>& rows) {
    setRowsRelaxed(rows, true);
}

void StableMapListModel::setRowsRelaxed(const QVector<QVariantMap>& rows, bool allowStructuralChanges) {
    if (!allowStructuralChanges && !m_rows.isEmpty()) {
        QHash<QString, QVariantMap> byKey;
        byKey.reserve(rows.size());
        for (const auto& row : rows) byKey.insert(rowKey(row), row);

        QVector<int> changedRows;
        changedRows.reserve(m_rows.size());
        for (int i = 0; i < m_rows.size(); ++i) {
            const QString key = rowKey(m_rows.at(i));
            const auto it = byKey.constFind(key);
            if (it == byKey.cend()) continue;
            if (m_rows.at(i) != it.value()) {
                m_rows[i] = it.value();
                changedRows.push_back(i);
            }
        }
        if (!changedRows.isEmpty()) {
            emitContiguousDataChanged(changedRows);
            emit rowsChanged();
        }
        return;
    }

    const int oldCount = m_rows.size();
    const int newCount = rows.size();

    const bool appendOnly = oldCount <= newCount && [&]() {
        for (int i = 0; i < oldCount; ++i) {
            if (rowKey(m_rows.at(i)) != rowKey(rows.at(i))) return false;
        }
        return true;
    }();
    const bool trimTailOnly = newCount <= oldCount && [&]() {
        for (int i = 0; i < newCount; ++i) {
            if (rowKey(m_rows.at(i)) != rowKey(rows.at(i))) return false;
        }
        return true;
    }();

    if (sameKeyOrder(rows)) {
        QVector<int> changedRows;
        changedRows.reserve(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            if (m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                changedRows.push_back(i);
            }
        }
        if (!changedRows.isEmpty()) {
            emitContiguousDataChanged(changedRows);
            emit rowsChanged();
        }
        return;
    }

    if (appendOnly && newCount > oldCount) {
        QVector<int> changedRows;
        changedRows.reserve(oldCount);
        for (int i = 0; i < oldCount; ++i) {
            if (m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                changedRows.push_back(i);
            }
        }
        if (!changedRows.isEmpty()) emitContiguousDataChanged(changedRows);
        beginInsertRows(QModelIndex(), oldCount, newCount - 1);
        for (int i = oldCount; i < newCount; ++i) m_rows.push_back(rows.at(i));
        endInsertRows();
        emit countChanged();
        emit rowsChanged();
        emit structureChanged();
        return;
    }

    if (trimTailOnly && newCount < oldCount) {
        QVector<int> changedRows;
        changedRows.reserve(newCount);
        for (int i = 0; i < newCount; ++i) {
            if (m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                changedRows.push_back(i);
            }
        }
        if (!changedRows.isEmpty()) emitContiguousDataChanged(changedRows);
        beginRemoveRows(QModelIndex(), newCount, oldCount - 1);
        while (m_rows.size() > newCount) m_rows.removeLast();
        endRemoveRows();
        emit countChanged();
        emit rowsChanged();
        emit structureChanged();
        return;
    }

    QSet<QString> newKeySet;
    newKeySet.reserve(newCount);
    for (const auto& row : rows) newKeySet.insert(rowKey(row));

    QStringList keptKeys;
    keptKeys.reserve(std::min(oldCount, newCount));
    for (const auto& row : m_rows) {
        const QString key = rowKey(row);
        if (newKeySet.contains(key)) keptKeys.push_back(key);
    }

    bool removableToPrefix = keptKeys.size() <= rows.size();
    for (int i = 0; removableToPrefix && i < keptKeys.size(); ++i) {
        if (keptKeys.at(i) != rowKey(rows.at(i))) removableToPrefix = false;
    }

    if (removableToPrefix) {
        bool structureChangedFlag = false;
        for (int i = m_rows.size() - 1; i >= 0; --i) {
            if (newKeySet.contains(rowKey(m_rows.at(i)))) continue;
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.removeAt(i);
            endRemoveRows();
            structureChangedFlag = true;
        }

        QVector<int> changedRows;
        const int commonCount = std::min(m_rows.size(), rows.size());
        changedRows.reserve(commonCount);
        for (int i = 0; i < commonCount; ++i) {
            if (rowKey(m_rows.at(i)) == rowKey(rows.at(i)) && m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                changedRows.push_back(i);
            }
        }
        if (!changedRows.isEmpty()) emitContiguousDataChanged(changedRows);

        if (rows.size() > m_rows.size()) {
            const int insertStart = m_rows.size();
            beginInsertRows(QModelIndex(), insertStart, rows.size() - 1);
            for (int i = insertStart; i < rows.size(); ++i) m_rows.push_back(rows.at(i));
            endInsertRows();
            structureChangedFlag = true;
        }

        if (structureChangedFlag) emit countChanged();
        if (structureChangedFlag || !changedRows.isEmpty()) emit rowsChanged();
        if (structureChangedFlag) emit structureChanged();
        return;
    }

    beginResetModel();
    m_rows = rows;
    endResetModel();
    if (oldCount != newCount) emit countChanged();
    emit rowsChanged();
    emit structureChanged();
}

void StableMapListModel::clear() {
    if (m_rows.isEmpty()) return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
    emit rowsChanged();
    emit structureChanged();
}

QVariantMap StableMapListModel::get(int indexValue) const {
    if (indexValue < 0 || indexValue >= m_rows.size()) return {};
    return m_rows.at(indexValue);
}

int StableMapListModel::indexOfKey(const QString& key) const {
    for (int i = 0; i < m_rows.size(); ++i) {
        if (rowKey(m_rows.at(i)) == key) return i;
    }
    return -1;
}


int StableMapListModel::findIndex(const QString& field, const QVariant& value) const {
    const QString target = value.toString();
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).value(field).toString() == target) return i;
    }
    return -1;
}

QStringList StableMapListModel::orderedKeys() const {
    QStringList out;
    out.reserve(m_rows.size());
    for (const auto& row : m_rows) out.push_back(rowKey(row));
    return out;
}

void StableMapListModel::emitContiguousDataChanged(const QVector<int>& rows) {
    if (rows.isEmpty()) return;
    int start = rows.front();
    int prev = rows.front();
    for (int i = 1; i < rows.size(); ++i) {
        const int row = rows.at(i);
        if (row == prev + 1) {
            prev = row;
            continue;
        }
        emit dataChanged(index(start, 0), index(prev, 0));
        start = prev = row;
    }
    emit dataChanged(index(start, 0), index(prev, 0));
}
