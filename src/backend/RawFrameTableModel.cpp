#include "RawFrameTableModel.h"

#include <algorithm>
#include <cstring>

namespace {
const QRegularExpression& idFilterSeparator() {
    static const QRegularExpression separator(QStringLiteral("[,;\\s]+"));
    return separator;
}

FrameRecord frameFromCanRaw(const TypedRecord& record, const TypedCanRawRecord& can) {
    FrameRecord frame;
    frame.tExtUs = can.monoUs;
    frame.canId = can.canId;
    frame.bus = can.bus;
    frame.dlc = can.dlc;
    frame.ext = can.extended;
    frame.rtr = can.rtr;
    frame.seq = quint8(record.header.seq & 0xFF);
    std::memcpy(frame.data, can.data, sizeof(frame.data));
    return frame;
}

FrameRecord frameFromSegmentEntry(const TypedRecord& record, const TypedCanRxSegmentEntry& entry) {
    FrameRecord frame;
    frame.tExtUs = entry.monoUs;
    frame.canId = entry.canId;
    frame.bus = entry.bus;
    frame.dlc = entry.dlc;
    frame.ext = entry.extended;
    frame.rtr = entry.rtr;
    frame.seq = quint8(record.header.seq & 0xFF);
    frame.hasCaptureSeq = true;
    frame.captureSeq = entry.captureSeq;
    std::memcpy(frame.data, entry.data, sizeof(frame.data));
    return frame;
}
}

RawFrameTableModel::RawFrameTableModel(QObject* parent)
    : QAbstractListModel(parent) {}

int RawFrameTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return hasActiveFilter() ? m_visibleRows.size() : int(m_tailRows.size());
}

const RawFrameTableModel::DisplayRow* RawFrameTableModel::displayRow(int row) const {
    if (row < 0 || row >= rowCount() || m_tailRows.empty()) return nullptr;
    quint64 sourceSeq = 0;
    if (hasActiveFilter()) sourceSeq = m_visibleRows.at(row);
    else sourceSeq = m_tailRows.front().ledgerSeq + quint64(row);
    const quint64 firstSeq = m_tailRows.front().ledgerSeq;
    if (sourceSeq < firstSeq) return nullptr;
    const quint64 offset = sourceSeq - firstSeq;
    if (offset >= quint64(m_tailRows.size())) return nullptr;
    return &m_tailRows[size_t(offset)];
}

QVariant RawFrameTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const DisplayRow* row = displayRow(index.row());
    if (!row) return unreadableRowValue(role, index.row() < 0 ? 0 : quint64(index.row()));
    const FrameRecord& frame = row->frame;

    switch (role) {
    case LedgerSeqRole: return QString::number(row->ledgerSeq);
    case RowNumberRole: return QString::number(row->ledgerSeq);
    case IdRole: return frame.canId;
    case IdTextRole: return idText(frame.canId);
    case BusRole: return int(frame.bus);
    case DlcRole: return int(frame.dlc);
    case DataHexRole: return hexBytes(frame.data, frame.dlc);
    case TimeUsRole: return QString::number(frame.tExtUs);
    case TimeTextRole: return formatElapsedUs(frame.tExtUs, m_firstTimeUs);
    case FlagsRole: {
        QStringList flags;
        if (frame.ext) flags << QStringLiteral("EXT");
        if (frame.rtr) flags << QStringLiteral("RTR");
        return flags.isEmpty() ? QStringLiteral("STD") : flags.join(QLatin1Char('|'));
    }
    case SourceRole: return QStringLiteral("truth");
    case ValidRole: return true;
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
        {SourceRole, "source"},
        {ValidRole, "valid"}
    };
}

QVariant RawFrameTableModel::unreadableRowValue(int role, quint64 sourceRow) {
    switch (role) {
    case LedgerSeqRole:
    case RowNumberRole:
        return QString::number(sourceRow);
    case IdRole:
        return QVariant::fromValue<quint32>(0);
    case IdTextRole:
        return QStringLiteral("LEDGER ERR");
    case BusRole:
    case DlcRole:
        return -1;
    case DataHexRole:
        return QStringLiteral("row read failed");
    case TimeUsRole:
    case TimeTextRole:
        return QStringLiteral("-");
    case FlagsRole:
        return QStringLiteral("INVALID");
    case SourceRole:
        return QStringLiteral("decoded-tail-error");
    case ValidRole:
        return false;
    default:
        return {};
    }
}

QString RawFrameTableModel::summary() const {
    const QString displayText = hasActiveFilter()
        ? QStringLiteral("filtered %1 / tail %2 / truth %3").arg(count()).arg(m_tailRows.size()).arg(m_totalRows)
        : QStringLiteral("tail %1 / truth %2").arg(m_tailRows.size()).arg(m_totalRows);
    QString text = QStringLiteral("%1 | decoded tail %2 KB | display dropped %3 | writer max %4 us")
        .arg(displayText)
        .arg(m_segmentBytes / 1024)
        .arg(droppedDisplayRows())
        .arg(m_writerMaxUs);
    if (m_writerQueueBytes > 0 || m_writerMaxQueueBytes > 0) {
        text += QStringLiteral(" | queue %1/max %2 B").arg(m_writerQueueBytes).arg(m_writerMaxQueueBytes);
    }
    if (m_writerFailures > 0 || m_writerOverrunBytes > 0) {
        text += QStringLiteral(" | writer ERR fail %1 overrun %2 B").arg(m_writerFailures).arg(m_writerOverrunBytes);
    }
    return text;
}

void RawFrameTableModel::clear() {
    resetLedgerState();
}

void RawFrameTableModel::resetLedgerState(const QString& sessionPath, const QString& error) {
    beginResetModel();
    m_tailRows.clear();
    m_visibleRows.clear();
    m_sessionPath = sessionPath;
    m_writerLastError = error;
    m_totalRows = 0;
    m_segmentBytes = 0;
    m_firstTimeUs = 0;
    m_latestSeq = 0;
    m_writerQueueBytes = 0;
    m_writerMaxQueueBytes = 0;
    m_writerOverrunBytes = 0;
    m_writerMaxUs = 0;
    m_writerFailures = error.isEmpty() ? 0 : 1;
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
    const quint64 firstSeq = m_totalRows;
    applyCommittedFrames(frames,
                         firstSeq,
                         firstSeq + quint64(frames.size()) - 1,
                         firstSeq + quint64(frames.size()),
                         m_segmentBytes,
                         m_sessionPath);
}

void RawFrameTableModel::applyCommittedFrames(const FrameRecordList& frames,
                                              quint64 firstSeq,
                                              quint64 lastSeq,
                                              quint64 totalRows,
                                              quint64 segmentBytes,
                                              const QString& sessionPath) {
    if (frames.isEmpty()) return;
    if (firstSeq != m_totalRows || totalRows < m_totalRows || lastSeq + 1 != totalRows) {
        resetLedgerState(sessionPath, QStringLiteral("raw ledger commit sequence mismatch"));
        firstSeq = 0;
        totalRows = quint64(frames.size());
        lastSeq = totalRows - 1;
    }

    for (const FrameRecord& frame : frames) {
        if (m_firstTimeUs == 0 || frame.tExtUs < m_firstTimeUs) m_firstTimeUs = frame.tExtUs;
    }
    appendTailRows(frames, firstSeq);
    m_totalRows = totalRows;
    m_segmentBytes = segmentBytes;
    m_sessionPath = sessionPath;
    m_latestSeq = lastSeq;
    emit totalRowsChanged();
    emit summaryChanged();
    emit rowsAppended(firstSeq, lastSeq, frames.size());
}

void RawFrameTableModel::applyCommittedTailFrames(const FrameRecordList& frames,
                                                  quint64 firstSeq,
                                                  quint64 totalRows,
                                                  quint64 segmentBytes,
                                                  const QString& sessionPath) {
    if (frames.isEmpty()) {
        if (totalRows < m_totalRows) {
            resetLedgerState(sessionPath, QStringLiteral("raw ledger commit sequence mismatch"));
            return;
        }
        m_totalRows = totalRows;
        m_segmentBytes = segmentBytes;
        m_sessionPath = sessionPath;
        m_latestSeq = totalRows == 0 ? 0 : totalRows - 1;
        emit totalRowsChanged();
        emit summaryChanged();
        return;
    }

    const quint64 frameCount = quint64(frames.size());
    if (totalRows < m_totalRows || firstSeq + frameCount > totalRows) {
        resetLedgerState(sessionPath, QStringLiteral("raw ledger commit sequence mismatch"));
        return;
    }

    for (const FrameRecord& frame : frames) {
        if (m_firstTimeUs == 0 || frame.tExtUs < m_firstTimeUs) m_firstTimeUs = frame.tExtUs;
    }

    const bool hasGap = !m_tailRows.empty() && firstSeq > m_tailRows.back().ledgerSeq + 1;
    if (hasGap) {
        beginResetModel();
        m_tailRows.clear();
        m_visibleRows.clear();
        const int start = frames.size() > kDisplayTailLimit ? frames.size() - kDisplayTailLimit : 0;
        for (int index = start; index < frames.size(); ++index) {
            const DisplayRow row{firstSeq + quint64(index), frames.at(index)};
            m_tailRows.push_back(row);
            if (hasActiveFilter() && rowMatches(row)) m_visibleRows.push_back(row.ledgerSeq);
        }
        endResetModel();
        emit countChanged();
    } else {
        appendTailRows(frames, firstSeq);
    }

    m_totalRows = totalRows;
    m_segmentBytes = segmentBytes;
    m_sessionPath = sessionPath;
    m_latestSeq = totalRows == 0 ? 0 : totalRows - 1;
    emit totalRowsChanged();
    emit summaryChanged();
    emit rowsAppended(firstSeq, firstSeq + frameCount - 1, frames.size());
}

void RawFrameTableModel::appendTailRows(const FrameRecordList& frames, quint64 firstSeq) {
    const int incoming = frames.size();
    const int overflow = std::max(0, int(m_tailRows.size()) + incoming - kDisplayTailLimit);
    if (overflow >= int(m_tailRows.size()) && incoming >= kDisplayTailLimit) {
        beginResetModel();
        m_tailRows.clear();
        const int start = incoming - kDisplayTailLimit;
        for (int index = start; index < incoming; ++index) {
            m_tailRows.push_back(DisplayRow{firstSeq + quint64(index), frames.at(index)});
        }
        m_visibleRows.clear();
        if (hasActiveFilter()) {
            m_visibleRows.reserve(int(m_tailRows.size()));
            for (const DisplayRow& row : m_tailRows) {
                if (rowMatches(row)) m_visibleRows.push_back(row.ledgerSeq);
            }
        }
        endResetModel();
        emit countChanged();
        return;
    }

    if (overflow > 0) {
        if (hasActiveFilter()) {
            const quint64 newFirstSeq = m_tailRows[size_t(overflow)].ledgerSeq;
            int visibleRemoveCount = 0;
            while (visibleRemoveCount < m_visibleRows.size() && m_visibleRows.at(visibleRemoveCount) < newFirstSeq) {
                ++visibleRemoveCount;
            }
            if (visibleRemoveCount > 0) {
                beginRemoveRows(QModelIndex(), 0, visibleRemoveCount - 1);
                m_visibleRows.erase(m_visibleRows.begin(), m_visibleRows.begin() + visibleRemoveCount);
                endRemoveRows();
            }
        } else {
            beginRemoveRows(QModelIndex(), 0, overflow - 1);
        }
        for (int index = 0; index < overflow; ++index) m_tailRows.pop_front();
        if (!hasActiveFilter()) endRemoveRows();
    }

    if (hasActiveFilter()) {
        QVector<DisplayRow> incomingRows;
        incomingRows.reserve(incoming);
        QVector<quint64> visibleSeqs;
        for (int index = 0; index < incoming; ++index) {
            DisplayRow row{firstSeq + quint64(index), frames.at(index)};
            if (rowMatches(row)) visibleSeqs.push_back(row.ledgerSeq);
            incomingRows.push_back(row);
        }
        for (const DisplayRow& row : incomingRows) m_tailRows.push_back(row);
        if (!visibleSeqs.isEmpty()) {
            const int first = m_visibleRows.size();
            beginInsertRows(QModelIndex(), first, first + visibleSeqs.size() - 1);
            m_visibleRows.append(visibleSeqs);
            endInsertRows();
        }
    } else {
        const int first = int(m_tailRows.size());
        beginInsertRows(QModelIndex(), first, first + incoming - 1);
        for (int index = 0; index < incoming; ++index) {
            m_tailRows.push_back(DisplayRow{firstSeq + quint64(index), frames.at(index)});
        }
        endInsertRows();
    }
    emit countChanged();
}

void RawFrameTableModel::updateWriterStatus(quint64 queueBytes,
                                            quint64 maxQueueBytes,
                                            quint64 overrunBytes,
                                            quint64 writeMaxUs,
                                            quint64 writeFailures,
                                            const QString& lastError) {
    m_writerQueueBytes = queueBytes;
    m_writerMaxQueueBytes = std::max(m_writerMaxQueueBytes, maxQueueBytes);
    m_writerOverrunBytes = overrunBytes;
    m_writerMaxUs = std::max(m_writerMaxUs, writeMaxUs);
    m_writerFailures = writeFailures;
    m_writerLastError = lastError;
    emit summaryChanged();
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
    const QString text = token.trimmed();
    if (text.isEmpty()) return false;
    bool ok = false;
    const quint32 value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
        ? text.mid(2).toUInt(&ok, 16)
        : text.toUInt(&ok, 10);
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

bool RawFrameTableModel::rowMatches(const DisplayRow& row) const {
    const FrameRecord& frame = row.frame;
    if (m_busFilter >= 0 && int(frame.bus) != m_busFilter) return false;
    if (m_idFilterTokens.isEmpty()) return true;
    QString normalizedIdText;
    for (const IdFilterToken& token : m_idFilterTokens) {
        if (token.hasId && token.id == frame.canId) return true;
        if (token.textUpper.isEmpty()) continue;
        if (normalizedIdText.isEmpty()) normalizedIdText = idText(frame.canId).toUpper();
        if (normalizedIdText.contains(token.textUpper)) return true;
    }
    return false;
}

void RawFrameTableModel::rebuildVisibleRows() {
    beginResetModel();
    m_visibleRows.clear();
    if (hasActiveFilter()) {
        m_visibleRows.reserve(int(m_tailRows.size()));
        for (const DisplayRow& row : m_tailRows) {
            if (rowMatches(row)) m_visibleRows.push_back(row.ledgerSeq);
        }
    }
    endResetModel();
    emit countChanged();
    emit summaryChanged();
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
