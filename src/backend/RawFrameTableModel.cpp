#include "RawFrameTableModel.h"

#include <algorithm>
#include <limits>

namespace {
const QRegularExpression& idFilterSeparator() {
    static const QRegularExpression separator(QStringLiteral("[,;\\s]+"));
    return separator;
}
}

RawFrameTableModel::RawFrameTableModel(QObject* parent)
    : QAbstractListModel(parent) {
    m_ledger.reset(QStringLiteral("live"));
}

int RawFrameTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    const quint64 rows = hasActiveFilter() ? quint64(m_visibleRows.size()) : m_ledger.rowCount();
    return int(std::min<quint64>(rows, quint64(std::numeric_limits<int>::max())));
}

QVariant RawFrameTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const quint64 sourceRow = sourceRowForDisplayRow(index.row());
    const auto row = m_ledger.readRow(sourceRow);
    if (!row) return {};

    switch (role) {
    case LedgerSeqRole: return QString::number(row->ledgerSeq);
    case RowNumberRole: return QString::number(sourceRow);
    case IdRole: return row->canId;
    case IdTextRole: return idText(row->canId);
    case BusRole: return int(row->bus);
    case DlcRole: return int(row->dlc);
    case DataHexRole: return hexBytes(row->data, row->dlc);
    case TimeUsRole: return QString::number(row->tExtUs);
    case TimeTextRole: return formatElapsedUs(row->tExtUs, m_firstTimeUs);
    case FlagsRole: {
        QStringList flags;
        if (row->ext) flags << QStringLiteral("EXT");
        if (row->rtr) flags << QStringLiteral("RTR");
        return flags.isEmpty() ? QStringLiteral("STD") : flags.join(QLatin1Char('|'));
    }
    case SourceRole: return QStringLiteral("truth");
    default: return {};
    }
}

QHash<int, QByteArray> RawFrameTableModel::roleNames() const {
    return {
        {LedgerSeqRole, "ledgerSeq"},
        {RowNumberRole, "rowNumber"},
        {IdRole, "id"},
        {IdTextRole, "idText"},
        {BusRole, "bus"},
        {DlcRole, "dlc"},
        {DataHexRole, "dataHex"},
        {TimeUsRole, "timeUs"},
        {TimeTextRole, "timeText"},
        {FlagsRole, "flags"},
        {SourceRole, "source"}
    };
}

QString RawFrameTableModel::summary() const {
    const QString filterText = hasActiveFilter()
        ? QStringLiteral("필터 표시 %1 / 원본 %2").arg(count()).arg(totalRows())
        : QStringLiteral("원본 %1").arg(totalRows());
    return QStringLiteral("%1 · 세션 ledger %2 KB · 표시 생략 0")
        .arg(filterText)
        .arg(segmentBytes() / 1024);
}

void RawFrameTableModel::clear() {
    beginResetModel();
    m_ledger.reset(QStringLiteral("live"));
    m_visibleRows.clear();
    m_firstTimeUs = 0;
    m_latestSeq = 0;
    endResetModel();
    emit countChanged();
    emit totalRowsChanged();
    emit summaryChanged();
}

void RawFrameTableModel::resetFilters() {
    bool changed = false;
    if (!m_idFilter.isEmpty()) {
        m_idFilter.clear();
        m_idFilterTokens.clear();
        changed = true;
    }
    if (m_busFilter >= 0) {
        m_busFilter = -1;
        changed = true;
    }
    if (!changed) return;
    rebuildVisibleRows();
    emit filtersChanged();
}

void RawFrameTableModel::appendFrames(const FrameRecordList& frames) {
    if (frames.isEmpty()) return;

    for (const FrameRecord& frame : frames) {
        if (m_firstTimeUs == 0 || frame.tExtUs < m_firstTimeUs) m_firstTimeUs = frame.tExtUs;
    }

    const quint64 oldTotal = totalRows();
    const auto result = m_ledger.appendFrames(frames);
    if (!result.ok || result.appended <= 0) {
        emit summaryChanged();
        return;
    }

    if (hasActiveFilter()) {
        QVector<quint64> appendedVisibleRows;
        appendedVisibleRows.reserve(result.appended);
        for (quint64 row = oldTotal; row < m_ledger.rowCount(); ++row) {
            if (rowMatches(row)) appendedVisibleRows.push_back(row);
        }
        if (!appendedVisibleRows.isEmpty()) {
            const int first = m_visibleRows.size();
            const int last = first + appendedVisibleRows.size() - 1;
            beginInsertRows(QModelIndex(), first, last);
            for (quint64 row : appendedVisibleRows) m_visibleRows.push_back(row);
            endInsertRows();
            emit countChanged();
        }
    } else {
        const int first = int(std::min<quint64>(oldTotal, quint64(std::numeric_limits<int>::max())));
        const int newCount = rowCount();
        if (newCount > first) {
            beginInsertRows(QModelIndex(), first, newCount - 1);
            endInsertRows();
        }
        if (newCount != first) emit countChanged();
    }
    m_latestSeq = result.lastSeq;
    emit totalRowsChanged();
    emit summaryChanged();
    emit rowsAppended(result.firstSeq, result.lastSeq, result.appended);
}

void RawFrameTableModel::appendTypedRecords(const TypedRecordList& records) {
    if (records.isEmpty()) return;

    quint64 firstUs = m_firstTimeUs;
    for (const TypedRecord& record : records) {
        if (record.isType(TypedRecordType::CanRxRaw)) {
            const auto can = decodeTypedCanRaw(record);
            if (!can) continue;
            if (firstUs == 0 || can->monoUs < firstUs) firstUs = can->monoUs;
        } else if (record.isType(TypedRecordType::CanRxSegment)) {
            const auto header = decodeTypedCanRxSegmentHeader(record);
            if (!header) continue;
            for (qsizetype index = 0; index < header->frameCount; ++index) {
                const auto entry = decodeTypedCanRxSegmentEntry(record, index);
                if (!entry) continue;
                if (firstUs == 0 || entry->monoUs < firstUs) firstUs = entry->monoUs;
            }
        }
    }
    if (firstUs != 0) m_firstTimeUs = firstUs;

    const quint64 oldTotal = totalRows();
    const auto result = m_ledger.appendTypedRecords(records);
    if (!result.ok || result.appended <= 0) {
        emit summaryChanged();
        return;
    }

    if (hasActiveFilter()) {
        QVector<quint64> appendedVisibleRows;
        appendedVisibleRows.reserve(result.appended);
        for (quint64 row = oldTotal; row < m_ledger.rowCount(); ++row) {
            if (rowMatches(row)) appendedVisibleRows.push_back(row);
        }
        if (!appendedVisibleRows.isEmpty()) {
            const int first = m_visibleRows.size();
            const int last = first + appendedVisibleRows.size() - 1;
            beginInsertRows(QModelIndex(), first, last);
            for (quint64 row : appendedVisibleRows) m_visibleRows.push_back(row);
            endInsertRows();
            emit countChanged();
        }
    } else {
        const int first = int(std::min<quint64>(oldTotal, quint64(std::numeric_limits<int>::max())));
        const int newCount = rowCount();
        if (newCount > first) {
            beginInsertRows(QModelIndex(), first, newCount - 1);
            endInsertRows();
        }
        if (newCount != first) emit countChanged();
    }
    m_latestSeq = result.lastSeq;
    emit totalRowsChanged();
    emit summaryChanged();
    emit rowsAppended(result.firstSeq, result.lastSeq, result.appended);
}

QVector<RawFrameTableModel::IdFilterToken> RawFrameTableModel::parseIdFilterTokens(const QString& text) {
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

bool RawFrameTableModel::parseTokenToId(const QString& token, quint32* out) {
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

QString RawFrameTableModel::formatElapsedUs(quint64 us, quint64 baseUs) {
    const quint64 elapsedUs = us >= baseUs ? (us - baseUs) : 0;
    const quint64 totalMs = elapsedUs / 1000ULL;
    const quint64 ms = totalMs % 1000ULL;
    const quint64 totalSec = totalMs / 1000ULL;
    const quint64 sec = totalSec % 60ULL;
    const quint64 min = (totalSec / 60ULL) % 60ULL;
    const quint64 hour = totalSec / 3600ULL;
    if (hour > 0) {
        return QStringLiteral("t+%1:%2:%3.%4")
            .arg(hour)
            .arg(min, 2, 10, QLatin1Char('0'))
            .arg(sec, 2, 10, QLatin1Char('0'))
            .arg(ms, 3, 10, QLatin1Char('0'));
    }
    return QStringLiteral("t+%1:%2.%3")
        .arg(min, 2, 10, QLatin1Char('0'))
        .arg(sec, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}

bool RawFrameTableModel::hasActiveFilter() const {
    return m_busFilter >= 0 || !m_idFilterTokens.isEmpty();
}

bool RawFrameTableModel::rowMatches(quint64 sourceRow) const {
    const auto row = m_ledger.readRow(sourceRow);
    if (!row) return false;
    if (m_busFilter >= 0 && int(row->bus) != m_busFilter) return false;
    if (m_idFilterTokens.isEmpty()) return true;

    QString normalizedIdText;
    for (const IdFilterToken& token : m_idFilterTokens) {
        if (token.hasId && token.id == row->canId) return true;
        if (token.textUpper.isEmpty()) continue;
        if (normalizedIdText.isEmpty()) normalizedIdText = idText(row->canId).toUpper();
        if (normalizedIdText.contains(token.textUpper)) return true;
    }
    return false;
}

void RawFrameTableModel::rebuildVisibleRows() {
    beginResetModel();
    m_visibleRows.clear();
    if (hasActiveFilter()) {
        m_visibleRows.reserve(qsizetype(std::min<quint64>(m_ledger.rowCount(), 1024 * 1024)));
        for (quint64 row = 0; row < m_ledger.rowCount(); ++row) {
            if (rowMatches(row)) m_visibleRows.push_back(row);
        }
    }
    endResetModel();
    emit countChanged();
    emit summaryChanged();
}

quint64 RawFrameTableModel::sourceRowForDisplayRow(int row) const {
    if (!hasActiveFilter()) return quint64(row);
    if (row < 0 || row >= m_visibleRows.size()) return 0;
    return m_visibleRows.at(row);
}

void RawFrameTableModel::setIdFilter(const QString& text) {
    const QString normalized = text.trimmed();
    if (m_idFilter == normalized) return;
    m_idFilter = normalized;
    m_idFilterTokens = parseIdFilterTokens(m_idFilter);
    rebuildVisibleRows();
    emit filtersChanged();
}

void RawFrameTableModel::setBusFilter(int bus) {
    const int normalized = bus < 0 ? -1 : bus;
    if (m_busFilter == normalized) return;
    m_busFilter = normalized;
    rebuildVisibleRows();
    emit filtersChanged();
}
