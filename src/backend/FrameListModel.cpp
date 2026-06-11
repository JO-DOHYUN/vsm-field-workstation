#include "FrameListModel.h"

#include <algorithm>

namespace {
QString formatElapsedUs(quint64 us) {
    const quint64 totalMs = us / 1000ULL;
    const quint64 ms = totalMs % 1000ULL;
    const quint64 totalSec = totalMs / 1000ULL;
    const quint64 sec = totalSec % 60ULL;
    const quint64 min = (totalSec / 60ULL) % 60ULL;
    const quint64 hour = totalSec / 3600ULL;
    if (hour > 0) return QStringLiteral("t+%1:%2:%3.%4").arg(hour).arg(min, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0')).arg(ms, 3, 10, QLatin1Char('0'));
    return QStringLiteral("t+%1:%2.%3").arg(min, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0')).arg(ms, 3, 10, QLatin1Char('0'));
}
}

FrameListModel::FrameListModel(QObject* parent) : QAbstractListModel(parent) {}

int FrameListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return int(m_rows.size());
}

QVariant FrameListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size())) return {};
    const Row& row = m_rows.at(size_t(index.row()));
    switch (role) {
    case IdRole: return row.fr.canId;
    case IdTextRole: return idText(row.fr.canId);
    case BusRole: return int(row.fr.bus);
    case DlcRole: return row.dlc;
    case DataHexRole: return row.dataHex;
    case TimeUsRole: return QString::number(row.fr.tExtUs);
    case TimeTextRole: return row.timeText.isEmpty() ? formatRelativeTime(row.fr.tExtUs) : row.timeText;
    case SourceRole: return row.source;
    default: return {};
    }
}

QHash<int, QByteArray> FrameListModel::roleNames() const {
    return {
        {IdRole, "id"},
        {IdTextRole, "idText"},
        {BusRole, "bus"},
        {DataHexRole, "dataHex"},
        {TimeUsRole, "timeUs"},
        {TimeTextRole, "timeText"},
        {SourceRole, "source"},
        {DlcRole, "dlc"}
    };
}

void FrameListModel::clear() {
    if (m_rows.empty() && m_firstTimeUs == 0) return;
    beginResetModel();
    m_rows.clear();
    m_firstTimeUs = 0;
    endResetModel();
    emit countChanged();
}

void FrameListModel::appendLive(const FrameRecord& fr, const QString& timeText) {
    append(fr, QStringLiteral("live"), timeText);
}

void FrameListModel::appendReplay(const FrameRecord& fr, const QString& timeText) {
    append(fr, QStringLiteral("replay"), timeText);
}

void FrameListModel::appendLiveBatch(const FrameRecordList& frames, const QStringList& timeTexts) {
    appendBatch(frames, QStringLiteral("live"), timeTexts);
}

void FrameListModel::appendReplayBatch(const FrameRecordList& frames, const QStringList& timeTexts) {
    appendBatch(frames, QStringLiteral("replay"), timeTexts);
}

void FrameListModel::append(const FrameRecord& fr, const QString& source, const QString& timeText) {
    if (m_firstTimeUs == 0 || fr.tExtUs < m_firstTimeUs) m_firstTimeUs = fr.tExtUs;
    beginInsertRows(QModelIndex(), 0, 0);
    m_rows.push_front({fr, source, timeText, hexBytes(fr.data, fr.dlc), int(std::clamp<int>(fr.dlc, 0, 8))});
    endInsertRows();

    if (int(m_rows.size()) > m_limit) {
        beginRemoveRows(QModelIndex(), m_limit, m_limit);
        m_rows.pop_back();
        endRemoveRows();
    }
    emit countChanged();
}

void FrameListModel::appendBatch(const FrameRecordList& frames, const QString& source, const QStringList& timeTexts) {
    if (frames.isEmpty()) return;

    for (const FrameRecord& fr : frames) {
        if (m_firstTimeUs == 0 || fr.tExtUs < m_firstTimeUs) m_firstTimeUs = fr.tExtUs;
    }

    const int insertCount = frames.size();
    beginInsertRows(QModelIndex(), 0, insertCount - 1);
    for (int i = 0; i < frames.size(); ++i) {
        const FrameRecord& fr = frames.at(i);
        const QString tt = (i < timeTexts.size()) ? timeTexts.at(i) : QString();
        m_rows.push_front({fr, source, tt, hexBytes(fr.data, fr.dlc), int(std::clamp<int>(fr.dlc, 0, 8))});
    }
    endInsertRows();

    const int overflow = std::max(0, int(m_rows.size()) - m_limit);
    if (overflow > 0) {
        const int first = int(m_rows.size()) - overflow;
        const int last = int(m_rows.size()) - 1;
        beginRemoveRows(QModelIndex(), first, last);
        for (int i = 0; i < overflow; ++i) {
            m_rows.pop_back();
        }
        endRemoveRows();
    }

    emit countChanged();
}

QString FrameListModel::formatRelativeTime(quint64 us) const {
    const quint64 base = m_firstTimeUs == 0 ? 0 : m_firstTimeUs;
    return formatElapsedUs(us >= base ? (us - base) : 0);
}
