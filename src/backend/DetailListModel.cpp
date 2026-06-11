#include "DetailListModel.h"

DetailListModel::DetailListModel(QObject* parent) : QAbstractListModel(parent) {}

int DetailListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_rows.size();
}

QVariant DetailListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const DetailRow& row = m_rows.at(index.row());
    switch (role) {
    case KeyRole: return row.key;
    case ValueRole: return row.value;
    case NoteRole: return row.note;
    default: return {};
    }
}

QHash<int, QByteArray> DetailListModel::roleNames() const {
    return {
        {KeyRole, "key"},
        {ValueRole, "value"},
        {NoteRole, "note"}
    };
}

void DetailListModel::clear() {
    if (m_rows.isEmpty()) return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

void DetailListModel::setRows(const QVector<DetailRow>& rows) {
    if (m_rows.size() != rows.size()) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        emit countChanged();
        return;
    }

    bool anyChanged = false;
    for (int i = 0; i < rows.size(); ++i) {
        if (m_rows.at(i) != rows.at(i)) {
            anyChanged = true;
            break;
        }
    }
    if (!anyChanged) return;

    m_rows = rows;
    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0));
    }
}
