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
constexpr int kRawLedgerCacheLimit = 8192;

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
    m_cacheOrderRing.clear();
    m_cacheOrderHead = 0;
    m_cacheOrderSize = 0;
    m_nextSeq = 0;
    m_rowCount = 0;
    m_blockCount = 0;
    m_segmentBytes = 0;
    m_cacheHits = 0;
    m_cacheMisses = 0;
    m_readFailCount = 0;
    m_fileReadFailCount = 0;
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

    QVector<QPair<QByteArray, quint16>> payloads;
    payloads.reserve(frames.size());
    for (const FrameRecord& frame : frames) {
        payloads.push_back(qMakePair(makeSegmentPayloadFromFrame(frame), quint16(frame.seq)));
    }
    return appendSegmentPayloads(payloads);
}

std::optional<RawLedgerRuntime::Row> RawLedgerRuntime::readRow(quint64 row) const {
    if (row >= rowCount() || !m_file.isOpen()) return noteReadFailure(false);
    const auto cached = m_cache.constFind(row);
    if (cached != m_cache.cend()) {
        ++m_cacheHits;
        return cached.value();
    }
    ++m_cacheMisses;

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
        if (!block) return noteReadFailure(true);
        if (block->firstRow <= row) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return noteReadFailure(true);
    const auto block = readBlock(lo - 1);
    if (!block) return noteReadFailure(true);
    const BlockRef& ref = *block;
    const quint64 offsetInBlock = row - ref.firstRow;
    if (offsetInBlock >= ref.frameCount || offsetInBlock > std::numeric_limits<quint16>::max()) {
        return noteReadFailure(true);
    }
    if (!m_file.seek(qint64(ref.blockOffset))) return noteReadFailure(true);
    const QByteArray header = m_file.read(kRawLedgerBlockHeaderSize);
    if (header.size() != kRawLedgerBlockHeaderSize) return noteReadFailure(true);
    if (rd32(header, 0) != kRawLedgerMagic || rd16(header, 4) != kRawLedgerVersion) return noteReadFailure(true);
    const quint16 payloadLength = rd16(header, 10);
    const QByteArray payload = m_file.read(payloadLength);
    if (payload.size() != payloadLength) return noteReadFailure(true);

    auto decoded = decodeRowFromPayload(payload, ref.typedSeq, quint16(offsetInBlock));
    if (decoded) {
        decoded->ledgerSeq = row;
        cacheRow(row, *decoded);
    } else {
        return noteReadFailure(true);
    }
    return decoded;
}

RawLedgerRuntime::Diagnostics RawLedgerRuntime::diagnostics() const {
    Diagnostics out;
    out.committedRows = m_rowCount;
    out.blocks = m_blockCount;
    out.segmentBytes = m_segmentBytes;
    out.cacheRows = m_cache.size();
    out.cacheHits = m_cacheHits;
    out.cacheMisses = m_cacheMisses;
    out.readFailCount = m_readFailCount;
    out.fileReadFailCount = m_fileReadFailCount;
    return out;
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

RawLedgerRuntime::AppendResult RawLedgerRuntime::appendSegmentPayloads(const QVector<QPair<QByteArray, quint16>>& payloads) {
    AppendResult result;
    if (payloads.isEmpty()) {
        result.ok = true;
        result.totalRows = m_rowCount;
        result.segmentBytes = m_segmentBytes;
        return result;
    }

    QByteArray segmentBatch;
    QByteArray indexBatch;
    QVector<Row> decodedRows;
    quint64 pendingRows = 0;
    quint64 pendingBlocks = 0;
    quint64 pendingSegmentBytes = 0;

    for (const auto& payloadAndSeq : payloads) {
        const QByteArray& segmentPayload = payloadAndSeq.first;
        const quint16 typedSeq = payloadAndSeq.second;
        TypedRecord record;
        record.header.recordType = static_cast<quint8>(TypedRecordType::CanRxSegment);
        record.header.payloadLength = quint16(segmentPayload.size());
        record.payload = segmentPayload;
        const auto header = decodeTypedCanRxSegmentHeader(record);
        if (!header) {
            result.error = QStringLiteral("raw ledger segment payload decode failed");
            return result;
        }

        const quint64 firstRow = m_rowCount + pendingRows;
        const quint64 blockOffset = m_segmentBytes + pendingSegmentBytes;
        for (qsizetype index = 0; index < header->frameCount; ++index) {
            auto decoded = decodeRowFromPayload(segmentPayload, typedSeq, quint16(index));
            if (!decoded) {
                result.error = QStringLiteral("raw ledger segment entry decode failed");
                return result;
            }
            decoded->ledgerSeq = firstRow + quint64(index);
            decodedRows.push_back(*decoded);
        }

        const QByteArray block = encodeBlock(segmentPayload, typedSeq);
        segmentBatch.append(block);
        wr64(indexBatch, firstRow);
        wr64(indexBatch, blockOffset);
        wr32(indexBatch, quint32(header->frameCount));
        wr16(indexBatch, typedSeq);
        wr16(indexBatch, 0);
        pendingRows += header->frameCount;
        ++pendingBlocks;
        pendingSegmentBytes += quint64(block.size());
    }

    if (indexBatch.size() != qint64(pendingBlocks * kRawLedgerIndexEntrySize)) {
        result.error = QStringLiteral("raw ledger index batch size mismatch");
        return result;
    }

    const qint64 oldSegmentSize = qint64(m_segmentBytes);
    const qint64 oldIndexSize = qint64(m_blockCount * kRawLedgerIndexEntrySize);
    auto writeAll = [](QFile& file, const QByteArray& bytes) {
        qint64 offset = 0;
        while (offset < bytes.size()) {
            const qint64 written = file.write(bytes.constData() + offset, bytes.size() - offset);
            if (written <= 0) return false;
            offset += written;
        }
        return true;
    };

    if (!m_file.seek(oldSegmentSize) || !writeAll(m_file, segmentBatch)) {
        m_file.resize(oldSegmentSize);
        m_file.seek(oldSegmentSize);
        m_lastError = QStringLiteral("raw ledger batch write failed: %1").arg(m_file.errorString());
        result.error = m_lastError;
        return result;
    }
    if (!m_indexFile.seek(oldIndexSize) || !writeAll(m_indexFile, indexBatch)) {
        m_file.resize(oldSegmentSize);
        m_file.seek(oldSegmentSize);
        m_indexFile.resize(oldIndexSize);
        m_indexFile.seek(oldIndexSize);
        m_lastError = QStringLiteral("raw ledger index batch write failed: %1").arg(m_indexFile.errorString());
        result.error = m_lastError;
        return result;
    }

    result.firstSeq = m_rowCount;
    result.committedFrames.reserve(decodedRows.size());
    for (const Row& row : decodedRows) {
        cacheRow(row.ledgerSeq, row);
        result.committedFrames.push_back(frameFromRow(row));
    }
    m_blockCount += pendingBlocks;
    m_rowCount += pendingRows;
    m_segmentBytes += pendingSegmentBytes;
    m_nextSeq = m_rowCount;
    result.ok = true;
    result.appended = int(std::min<quint64>(pendingRows, quint64(std::numeric_limits<int>::max())));
    result.lastSeq = m_rowCount == 0 ? 0 : m_rowCount - 1;
    result.totalRows = rowCount();
    result.segmentBytes = segmentBytes();
    return result;
}

FrameRecord RawLedgerRuntime::frameFromRow(const Row& row) {
    FrameRecord frame;
    frame.tExtUs = row.tExtUs;
    frame.canId = row.canId;
    frame.bus = row.bus;
    frame.dlc = row.dlc;
    frame.ext = row.ext;
    frame.rtr = row.rtr;
    frame.seq = row.sourceSeq;
    frame.hasCaptureSeq = row.hasCaptureSeq;
    frame.captureSeq = row.captureSeq;
    std::memcpy(frame.data, row.data, sizeof(frame.data));
    return frame;
}

void RawLedgerRuntime::cacheRow(quint64 row, const Row& value) const {
    if (m_cache.contains(row)) {
        m_cache.insert(row, value);
        return;
    }

    if (m_cacheOrderRing.size() != kRawLedgerCacheLimit) {
        m_cacheOrderRing.resize(kRawLedgerCacheLimit);
        m_cacheOrderHead = 0;
        m_cacheOrderSize = 0;
    }
    if (m_cacheOrderSize == kRawLedgerCacheLimit) {
        const quint64 evict = m_cacheOrderRing.at(m_cacheOrderHead);
        m_cache.remove(evict);
        m_cacheOrderRing[m_cacheOrderHead] = row;
        m_cacheOrderHead = (m_cacheOrderHead + 1) % kRawLedgerCacheLimit;
    } else {
        const int slot = (m_cacheOrderHead + m_cacheOrderSize) % kRawLedgerCacheLimit;
        m_cacheOrderRing[slot] = row;
        ++m_cacheOrderSize;
    }
    m_cache.insert(row, value);
}

std::optional<RawLedgerRuntime::Row> RawLedgerRuntime::noteReadFailure(bool fileAttempted) const {
    ++m_readFailCount;
    if (fileAttempted) ++m_fileReadFailCount;
    return std::nullopt;
}

} // namespace CanMonitorTransport
