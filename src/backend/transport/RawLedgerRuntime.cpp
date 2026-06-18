#include "transport/RawLedgerRuntime.h"

#include <QDateTime>
#include <QDir>
#include <QIODevice>

#include <algorithm>
#include <cstring>
#include <limits>

namespace {
constexpr quint32 kRawLedgerMagic = 0x524C4452; // RLDR
constexpr quint16 kRawLedgerVersion = 1;
constexpr int kRawLedgerBlockHeaderSize = 16;
constexpr int kRawLedgerIndexEntrySize = 24;
constexpr int kRawLedgerCacheLimit = 4096;

void wr16(QByteArray& out, quint16 value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void wr32(QByteArray& out, quint32 value) {
    for (int byte = 0; byte < 4; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

void wr64(QByteArray& out, quint64 value) {
    for (int byte = 0; byte < 8; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

quint16 rd16(const QByteArray& bytes, qsizetype offset) {
    const auto* p = reinterpret_cast<const quint8*>(bytes.constData() + offset);
    return typedReadU16Le(p);
}

quint32 rd32(const QByteArray& bytes, qsizetype offset) {
    const auto* p = reinterpret_cast<const quint8*>(bytes.constData() + offset);
    return typedReadU32Le(p);
}
}

namespace CanMonitorTransport {

RawLedgerRuntime::RawLedgerRuntime() {
    reset();
}

RawLedgerRuntime::~RawLedgerRuntime() {
    if (m_file.isOpen()) m_file.close();
    if (m_indexFile.isOpen()) m_indexFile.close();
}

bool RawLedgerRuntime::reset(const QString& label) {
    if (m_file.isOpen()) m_file.close();
    if (m_indexFile.isOpen()) m_indexFile.close();
    m_cache.clear();
    m_cacheOrder.clear();
    m_nextSeq = 0;
    m_rowCount = 0;
    m_blockCount = 0;
    m_segmentBytes = 0;
    m_lastError.clear();

    const QString safeLabel = label.trimmed().isEmpty()
        ? QStringLiteral("session")
        : label.trimmed().left(48).replace(QChar('/'), QChar('_')).replace(QChar('\\'), QChar('_'));
    const QString pattern = QDir::tempPath() + QStringLiteral("/vsm_raw_ledger_%1_XXXXXX").arg(safeLabel);
    m_tempDir.reset(new QTemporaryDir(pattern));
    if (!m_tempDir->isValid()) {
        m_lastError = QStringLiteral("raw ledger temp dir failed");
        m_segmentPath.clear();
        return false;
    }

    m_segmentPath = m_tempDir->path() + QStringLiteral("/raw_ledger.segment");
    m_indexPath = m_tempDir->path() + QStringLiteral("/raw_ledger.index");
    m_file.setFileName(m_segmentPath);
    if (!m_file.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("raw ledger open failed: %1").arg(m_file.errorString());
        return false;
    }
    m_indexFile.setFileName(m_indexPath);
    if (!m_indexFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("raw ledger index open failed: %1").arg(m_indexFile.errorString());
        return false;
    }
    return true;
}

RawLedgerRuntime::AppendResult RawLedgerRuntime::appendFrames(const FrameRecordList& frames) {
    AppendResult result;
    if (frames.isEmpty()) {
        result.ok = true;
        result.totalRows = rowCount();
        result.segmentBytes = segmentBytes();
        return result;
    }
    if (!m_file.isOpen() && !reset()) {
        result.error = m_lastError;
        return result;
    }

    result.firstSeq = m_nextSeq;
    for (const FrameRecord& frame : frames) {
        const auto appended = appendSegmentPayload(makeSegmentPayloadFromFrame(frame), frame.seq);
        if (!appended.ok) {
            result.error = appended.error;
            return result;
        }
        result.appended += appended.appended;
        result.totalRows = appended.totalRows;
        result.segmentBytes = appended.segmentBytes;
    }
    result.ok = true;
    result.lastSeq = m_rowCount == 0 ? 0 : m_rowCount - 1;
    result.totalRows = rowCount();
    result.segmentBytes = segmentBytes();
    return result;
}

RawLedgerRuntime::AppendResult RawLedgerRuntime::appendTypedRecords(const TypedRecordList& records) {
    AppendResult result;
    if (records.isEmpty()) {
        result.ok = true;
        result.totalRows = rowCount();
        result.segmentBytes = segmentBytes();
        return result;
    }
    if (!m_file.isOpen() && !reset()) {
        result.error = m_lastError;
        return result;
    }

    result.firstSeq = m_rowCount;
    for (const TypedRecord& record : records) {
        QByteArray segmentPayload;
        if (record.isType(TypedRecordType::CanRxSegment)) {
            if (!decodeTypedCanRxSegmentHeader(record)) continue;
            segmentPayload = record.payload;
        } else if (record.isType(TypedRecordType::CanRxRaw)) {
            const auto can = decodeTypedCanRaw(record);
            if (!can) continue;
            segmentPayload = makeSegmentPayloadFromCanRaw(record, *can);
        } else {
            continue;
        }

        const auto appended = appendSegmentPayload(segmentPayload, record.header.seq);
        if (!appended.ok) {
            result.error = appended.error;
            return result;
        }
        result.appended += appended.appended;
        result.totalRows = appended.totalRows;
        result.segmentBytes = appended.segmentBytes;
    }
    result.ok = true;
    result.lastSeq = m_rowCount == 0 ? 0 : m_rowCount - 1;
    result.totalRows = rowCount();
    result.segmentBytes = segmentBytes();
    return result;
}

std::optional<RawLedgerRuntime::Row> RawLedgerRuntime::readRow(quint64 row) const {
    if (row >= rowCount() || !m_file.isOpen()) return std::nullopt;
    const auto cached = m_cache.constFind(row);
    if (cached != m_cache.cend()) return cached.value();

    auto readBlock = [this](quint64 blockIndex) -> std::optional<BlockRef> {
        if (blockIndex >= m_blockCount) return std::nullopt;
        if (!m_indexFile.seek(qint64(blockIndex * kRawLedgerIndexEntrySize))) return std::nullopt;
        const QByteArray entry = m_indexFile.read(kRawLedgerIndexEntrySize);
        if (entry.size() != kRawLedgerIndexEntrySize) return std::nullopt;
        BlockRef block;
        block.firstRow = 0;
        block.blockOffset = 0;
        block.frameCount = 0;
        block.typedSeq = 0;
        for (int byte = 0; byte < 8; ++byte) {
            block.firstRow |= quint64(quint8(entry.at(byte))) << (byte * 8);
            block.blockOffset |= quint64(quint8(entry.at(8 + byte))) << (byte * 8);
        }
        block.frameCount = rd32(entry, 16);
        block.typedSeq = rd16(entry, 20);
        return block;
    };

    quint64 lo = 0;
    quint64 hi = m_blockCount;
    while (lo < hi) {
        const quint64 mid = lo + ((hi - lo) / 2);
        const auto block = readBlock(mid);
        if (!block) return std::nullopt;
        if (block->firstRow <= row) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return std::nullopt;
    const auto block = readBlock(lo - 1);
    if (!block) return std::nullopt;
    const BlockRef& ref = *block;
    const quint64 offsetInBlock = row - ref.firstRow;
    if (offsetInBlock >= ref.frameCount || offsetInBlock > std::numeric_limits<quint16>::max()) {
        return std::nullopt;
    }
    if (!m_file.seek(qint64(ref.blockOffset))) return std::nullopt;
    const QByteArray header = m_file.read(kRawLedgerBlockHeaderSize);
    if (header.size() != kRawLedgerBlockHeaderSize) return std::nullopt;
    if (rd32(header, 0) != kRawLedgerMagic || rd16(header, 4) != kRawLedgerVersion) return std::nullopt;
    const quint16 payloadLength = rd16(header, 10);
    const QByteArray payload = m_file.read(payloadLength);
    if (payload.size() != payloadLength) return std::nullopt;

    auto decoded = decodeRowFromPayload(payload, ref.typedSeq, quint16(offsetInBlock));
    if (decoded) {
        decoded->ledgerSeq = row;
        cacheRow(row, *decoded);
    }
    return decoded;
}

QByteArray RawLedgerRuntime::makeSegmentPayloadFromFrame(const FrameRecord& frame) {
    QByteArray payload;
    payload.reserve(int(kTypedCanRxSegmentHeaderSize + kTypedCanRxSegmentEntrySize));
    const quint64 captureSeq = frame.hasCaptureSeq ? frame.captureSeq : 0;
    wr64(payload, 0);
    wr64(payload, captureSeq);
    wr16(payload, 1);
    payload.append(char(kTypedCanRxSegmentEntrySize));
    payload.append(char(frame.hasCaptureSeq ? 0x01 : 0x00));
    wr32(payload, 0);
    wr32(payload, 0);
    wr32(payload, 0);
    wr64(payload, captureSeq);
    wr64(payload, frame.tExtUs);
    quint32 canIdFlags = frame.canId & 0x1FFFFFFFu;
    if (frame.ext) canIdFlags |= (1u << 29);
    if (frame.rtr) canIdFlags |= (1u << 30);
    wr32(payload, canIdFlags);
    payload.append(char(frame.dlc & 0x0F));
    payload.append(char(frame.bus));
    payload.append(reinterpret_cast<const char*>(frame.data), 8);
    return payload;
}

QByteArray RawLedgerRuntime::makeSegmentPayloadFromCanRaw(const TypedRecord& record, const TypedCanRawRecord& can) {
    FrameRecord frame;
    frame.tExtUs = can.monoUs;
    frame.canId = can.canId;
    frame.ext = can.extended;
    frame.rtr = can.rtr;
    frame.dlc = can.dlc;
    frame.bus = can.bus;
    frame.seq = quint8(record.header.seq & 0xFF);
    std::memcpy(frame.data, can.data, sizeof(frame.data));
    return makeSegmentPayloadFromFrame(frame);
}

QByteArray RawLedgerRuntime::encodeBlock(const QByteArray& segmentPayload, quint16 typedSeq) {
    QByteArray out;
    out.reserve(kRawLedgerBlockHeaderSize + segmentPayload.size());
    wr32(out, kRawLedgerMagic);
    wr16(out, kRawLedgerVersion);
    out.append(char(static_cast<quint8>(TypedRecordType::CanRxSegment)));
    out.append(char(0));
    wr16(out, quint16(kTypedCanRxSegmentEntrySize));
    wr16(out, quint16(segmentPayload.size()));
    wr16(out, typedSeq);
    wr16(out, 0);
    out.append(segmentPayload);
    return out;
}

std::optional<RawLedgerRuntime::Row> RawLedgerRuntime::decodeRowFromPayload(const QByteArray& segmentPayload,
                                                                           quint16 typedSeq,
                                                                           quint16 frameIndex) {
    TypedRecord record;
    record.header.recordType = static_cast<quint8>(TypedRecordType::CanRxSegment);
    record.header.payloadLength = quint16(segmentPayload.size());
    record.payload = segmentPayload;
    const auto header = decodeTypedCanRxSegmentHeader(record);
    const auto entry = decodeTypedCanRxSegmentEntry(record, frameIndex);
    if (!entry) return std::nullopt;

    Row row;
    row.tExtUs = entry->monoUs;
    row.canId = entry->canId;
    row.bus = entry->bus;
    row.dlc = entry->dlc;
    row.ext = entry->extended;
    row.rtr = entry->rtr;
    row.sourceSeq = quint8(typedSeq & 0xFF);
    row.hasCaptureSeq = header && ((header->flags & 0x01) != 0);
    row.captureSeq = entry->captureSeq;
    std::memcpy(row.data, entry->data, sizeof(row.data));
    return row;
}

RawLedgerRuntime::AppendResult RawLedgerRuntime::appendSegmentPayload(const QByteArray& segmentPayload, quint16 typedSeq) {
    AppendResult result;
    TypedRecord record;
    record.header.recordType = static_cast<quint8>(TypedRecordType::CanRxSegment);
    record.header.payloadLength = quint16(segmentPayload.size());
    record.payload = segmentPayload;
    const auto header = decodeTypedCanRxSegmentHeader(record);
    if (!header) {
        result.error = QStringLiteral("raw ledger segment payload decode failed");
        return result;
    }
    for (qsizetype index = 0; index < header->frameCount; ++index) {
        if (!decodeTypedCanRxSegmentEntry(record, index)) {
            result.error = QStringLiteral("raw ledger segment entry decode failed");
            return result;
        }
    }

    const QByteArray block = encodeBlock(segmentPayload, typedSeq);
    const quint64 blockOffset = m_segmentBytes;
    const qint64 written = m_file.write(block);
    if (written != block.size()) {
        m_lastError = QStringLiteral("raw ledger write failed: %1").arg(m_file.errorString());
        result.error = m_lastError;
        return result;
    }

    QByteArray indexEntry;
    indexEntry.reserve(kRawLedgerIndexEntrySize);
    wr64(indexEntry, m_rowCount);
    wr64(indexEntry, blockOffset);
    wr32(indexEntry, quint32(header->frameCount));
    wr16(indexEntry, typedSeq);
    wr16(indexEntry, 0);
    if (indexEntry.size() != kRawLedgerIndexEntrySize) {
        m_lastError = QStringLiteral("raw ledger index entry size mismatch");
        result.error = m_lastError;
        return result;
    }
    if (!m_indexFile.seek(qint64(m_blockCount * kRawLedgerIndexEntrySize))) {
        m_lastError = QStringLiteral("raw ledger index seek failed: %1").arg(m_indexFile.errorString());
        result.error = m_lastError;
        return result;
    }
    if (m_indexFile.write(indexEntry) != indexEntry.size()) {
        m_lastError = QStringLiteral("raw ledger index write failed: %1").arg(m_indexFile.errorString());
        result.error = m_lastError;
        return result;
    }
    ++m_blockCount;
    m_rowCount += header->frameCount;
    m_segmentBytes += quint64(block.size());
    m_nextSeq = m_rowCount;
    result.ok = true;
    result.appended = header->frameCount;
    result.firstSeq = m_rowCount >= header->frameCount ? m_rowCount - header->frameCount : 0;
    result.lastSeq = m_rowCount == 0 ? 0 : m_rowCount - 1;
    result.totalRows = rowCount();
    result.segmentBytes = segmentBytes();
    return result;
}

void RawLedgerRuntime::cacheRow(quint64 row, const Row& value) const {
    if (!m_cache.contains(row)) {
        m_cacheOrder.push_back(row);
    }
    m_cache.insert(row, value);
    while (m_cacheOrder.size() > kRawLedgerCacheLimit) {
        const quint64 evict = m_cacheOrder.takeFirst();
        m_cache.remove(evict);
    }
}

} // namespace CanMonitorTransport
