#include "FrameFilterProxyModel.h"

#include "FrameListModel.h"

#include <QAbstractItemModel>
#include <QRegularExpression>

namespace {
const QRegularExpression& idFilterSeparator() {
    static const QRegularExpression separator(QStringLiteral("[,;\\s]+"));
    return separator;
}

bool parseTokenToId(const QString& token, quint32* out) {
    if (!out) return false;
    const QString t = token.trimmed();
    if (t.isEmpty()) return false;
    bool ok = false;
    quint32 value = 0;
    if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) value = t.mid(2).toUInt(&ok, 16);
    else value = t.toUInt(&ok, 10);
    if (!ok) return false;
    *out = value;
    return true;
}
}

QVector<FrameFilterProxyModel::IdFilterToken> FrameFilterProxyModel::parseIdFilterTokens(const QString& text) {
    QVector<IdFilterToken> out;
    const QStringList tokens = text.split(idFilterSeparator(), Qt::SkipEmptyParts);
    out.reserve(tokens.size());
    for (const QString& rawToken : tokens) {
        const QString token = rawToken.trimmed();
        if (token.isEmpty()) continue;
        IdFilterToken parsed;
        parsed.textUpper = token.toUpper();
        parsed.hasId = parseTokenToId(token, &parsed.id);
        out.push_back(parsed);
    }
    return out;
}

FrameFilterProxyModel::FrameFilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    connect(this, &QSortFilterProxyModel::modelReset, this, &FrameFilterProxyModel::countChanged);
    connect(this, &QSortFilterProxyModel::rowsInserted, this, &FrameFilterProxyModel::countChanged);
    connect(this, &QSortFilterProxyModel::rowsRemoved, this, &FrameFilterProxyModel::countChanged);
    connect(this, &QSortFilterProxyModel::layoutChanged, this, &FrameFilterProxyModel::countChanged);
}

void FrameFilterProxyModel::setIdFilter(const QString& text) {
    const QString normalized = text.trimmed();
    if (m_idFilter == normalized) return;
    m_idFilter = normalized;
    m_idFilterTokens = parseIdFilterTokens(m_idFilter);
    emit idFilterChanged();
    invalidate();
    emit countChanged();
}

void FrameFilterProxyModel::setBusFilter(int bus) {
    const int normalized = bus < 0 ? -1 : bus;
    if (m_busFilter == normalized) return;
    m_busFilter = normalized;
    emit busFilterChanged();
    invalidate();
    emit countChanged();
}

QHash<int, QByteArray> FrameFilterProxyModel::roleNames() const {
    const QAbstractItemModel* src = sourceModel();
    return src ? src->roleNames() : QSortFilterProxyModel::roleNames();
}

bool FrameFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QAbstractItemModel* src = sourceModel();
    if (!src) return true;

    const QModelIndex idx = src->index(sourceRow, 0, sourceParent);
    if (m_busFilter >= 0 && src->data(idx, FrameListModel::BusRole).toInt() != m_busFilter) {
        return false;
    }

    if (m_idFilterTokens.isEmpty()) return true;

    const QString idText = src->data(idx, FrameListModel::IdTextRole).toString();
    const quint32 idValue = src->data(idx, FrameListModel::IdRole).toUInt();
    QString normalizedIdText;

    for (const IdFilterToken& token : m_idFilterTokens) {
        if (token.hasId && token.id == idValue) return true;
        if (token.textUpper.isEmpty()) continue;
        if (normalizedIdText.isEmpty()) normalizedIdText = idText.toUpper();
        if (normalizedIdText.contains(token.textUpper)) return true;
    }
    return false;
}
